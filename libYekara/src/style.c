#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <Xm/XmAll.h>

#include "yk/style.h"


/* Ensure the layout tag macro is available for modern OpenMotif matching */
#ifndef _MOTIF_DEFAULT_LOCALE_TAG
#define _MOTIF_DEFAULT_LOCALE_TAG "MOTIF_DEFAULT_LOCALE"
#endif


/* ==========================================================
   Yekara resource system
   ========================================================== */

static Atom YkResourceAtom;


/*
 * Root window used for automatic transient discovery.
 */
static Window YkRootWindow = None;


/*
 * Whether the Create/Map/Configure/Destroy dispatchers have
 * already been installed.
 *
 * YkResourceSysInit() may legitimately be called more than
 * once (once per top-level shell the application creates),
 * but the dispatcher chain must only be installed once, or
 * each additional call would wrap the previous wrapper and
 * every event would be processed N times.
 */
static int YkDispatchersInstalled = 0;


/*
 * Original Xt event dispatchers.
 *
 * We replace the dispatchers for CreateNotify, MapNotify,
 * ConfigureNotify and DestroyNotify so libyk can observe
 * newly-created top-level windows and track their lifecycle,
 * while still allowing normal Xt processing to continue.
 */
static XtEventDispatchProc YkOldCreateDispatcher = NULL;
static XtEventDispatchProc YkOldMapDispatcher = NULL;
static XtEventDispatchProc YkOldConfigureDispatcher = NULL;
static XtEventDispatchProc YkOldDestroyDispatcher = NULL;


/*
 * Cached global render table built from the current theme.
 *
 * Kept alive between reloads (rather than freed immediately
 * after YkReloadResources() runs) so that newly-discovered
 * transients can be styled with it as soon as they appear,
 * without needing a full reload to happen first.
 */
static XmRenderTable YkGlobalRenderTable = NULL;


/* ==========================================================
   Theme structure
   ========================================================== */

typedef struct {
    Pixel background;
    Pixel foreground;
    Pixel highlight;
    Pixel shadow;

    char font_family[128];

    int font_size;
    int valid;
} YkTheme;


static YkTheme YkCurrentTheme;


/* ==========================================================
   Known-window set
   ==========================================================

   A window is "known" to libyk if it is either:

     - a top-level shell explicitly handed to
       YkResourceSysInit() by the application, or

     - a transient dialog that libyk has already registered
       as belonging to a known window.

   WM_TRANSIENT_FOR is checked against this set rather than
   against a single hardcoded application window, so that:

     1) an application with more than one independent
        top-level shell (e.g. a main window plus a separate
        toolbox/palette shell) can register each of them, and

     2) nested dialogs (a dialog spawned from another dialog)
        are automatically recognized, since the first dialog
        becomes a known window the moment it is registered.

   This means no manual "this dialog belongs to libyk" call
   is required anywhere in application code beyond the initial
   YkResourceSysInit() per top-level shell.
   ========================================================== */

typedef struct YkKnownWindow {
    Window window;
    struct YkKnownWindow *next;
} YkKnownWindow;


static YkKnownWindow *YkKnownWindows = NULL;


static int YkIsKnownWindow(
    Window window)
{
    YkKnownWindow *k;

    k = YkKnownWindows;

    while (k) {
        if (k->window == window)
            return 1;

        k = k->next;
    }

    return 0;
}


static void YkAddKnownWindow(
    Window window)
{
    YkKnownWindow *k;

    if (window == None)
        return;

    if (YkIsKnownWindow(window))
        return;

    k = calloc(1, sizeof(YkKnownWindow));

    if (!k)
        return;

    k->window = window;

    k->next = YkKnownWindows;

    YkKnownWindows = k;
}


static void YkRemoveKnownWindow(
    Window window)
{
    YkKnownWindow **current;
    YkKnownWindow *dead;

    current = &YkKnownWindows;

    while (*current) {

        if ((*current)->window == window) {
            dead = *current;

            *current = dead->next;

            free(dead);

            return;
        }

        current = &(*current)->next;
    }
}


/* ==========================================================
   Yekara transient/dialog system
   ========================================================== */


typedef struct YkTransientInfo {
    Window window;
    Window parent;

    int width;
    int height;

    struct YkTransientInfo *next;
} YkTransientInfo;


static YkTransientInfo *YkTransients = NULL;


/* ----------------------------------------------------------
   Find registered transient
---------------------------------------------------------- */

static YkTransientInfo *YkFindTransient(
    Window window)
{
    YkTransientInfo *info;

    info = YkTransients;

    while (info) {
        if (info->window == window)
            return info;

        info = info->next;
    }

    return NULL;
}


/* ----------------------------------------------------------
   Remove registered transient
---------------------------------------------------------- */

static void YkRemoveTransient(
    Window window)
{
    YkTransientInfo **current;
    YkTransientInfo *dead;

    current = &YkTransients;

    while (*current) {

        if ((*current)->window == window) {
            dead = *current;

            *current = dead->next;

            free(dead);

            /*
             * A destroyed transient is no longer a valid
             * parent for anything, and its window ID may be
             * reused by the X server later.
             */
            YkRemoveKnownWindow(window);

            return;
        }

        current = &(*current)->next;
    }
}


/* ----------------------------------------------------------
   Center transient
---------------------------------------------------------- */

/* ----------------------------------------------------------
   Mark our chosen position as explicit

   Without this, a top-level window's WM_NORMAL_HINTS carries
   no USPosition/PPosition flag, and most window managers treat
   that as "the app doesn't care where this goes" -- triggering
   their own smart-placement/cascade logic, independently and
   sometimes slightly asynchronously from the map itself. That
   race is what produces a one-time jump away from wherever we
   just centered the window: we move it first, the WM's own
   placement policy overrides us a moment later, and only then
   does our ConfigureNotify-driven correction move it back.

   Declaring the position explicit heads the WM's own placement
   logic off entirely, rather than just winning the race after
   the fact. Existing hints (min/max size, resize increments,
   gravity, ...) that Motif already set are preserved -- only
   the position fields are added.
---------------------------------------------------------- */





/* ----------------------------------------------------------
   Restore original transient size
---------------------------------------------------------- */

static void YkRestoreTransientSize(
    Display *dpy,
    YkTransientInfo *info)
{
    XWindowAttributes attr;


    if (!dpy || !info)
        return;


    if (!XGetWindowAttributes(
            dpy,
            info->window,
            &attr))
    {
        return;
    }


    /*
     * Only resize if the actual dimensions differ.
     */
    if (attr.width == info->width &&
        attr.height == info->height)
    {
        return;
    }


    XResizeWindow(
        dpy,
        info->window,
        info->width,
        info->height
    );
}


/* ----------------------------------------------------------
   Handle ConfigureNotify for a transient
---------------------------------------------------------- */

static void YkHandleTransientConfigure(
    Display *dpy,
    Window window)
{
    YkTransientInfo *info;


    info =
        YkFindTransient(window);


    if (!info)
        return;


    /*
     * Restore size first.
     *
     * If the application asked for a different size,
     * libyk immediately restores the original size.
     */
    YkRestoreTransientSize(
        dpy,
        info
    );


    XFlush(dpy);
}


/* ==========================================================
   Color helpers
========================================================== */

static void YkQueryRGB(
    Display *dpy,
    Pixel p,
    int *r,
    int *g,
    int *b)
{
    XColor c;


    c.pixel =
        p;


    XQueryColor(
        dpy,
        DefaultColormap(
            dpy,
            DefaultScreen(dpy)
        ),
        &c
    );


    *r =
        c.red >> 8;

    *g =
        c.green >> 8;

    *b =
        c.blue >> 8;
}


static Pixel YkAllocRGB(
    Display *dpy,
    int r,
    int g,
    int b)
{
    XColor c;

    Colormap cmap;


    cmap =
        DefaultColormap(
            dpy,
            DefaultScreen(dpy)
        );


    c.red =
        r << 8;

    c.green =
        g << 8;

    c.blue =
        b << 8;


    if (XAllocColor(
            dpy,
            cmap,
            &c))
    {
        return c.pixel;
    }


    return BlackPixel(
        dpy,
        DefaultScreen(dpy)
    );
}


static void YkComputeBevels(
    Display *dpy,
    Pixel bg,
    Pixel *hi,
    Pixel *sh)
{
    int r;
    int g;
    int b;

    int hr;
    int hg;
    int hb;

    int sr;
    int sg;
    int sb;

    int brightness;


    YkQueryRGB(
        dpy,
        bg,
        &r,
        &g,
        &b
    );


    brightness =
        (r * 299 +
         g * 587 +
         b * 114) / 10000;


    if (brightness >= 85) {

        hr =
            r * 0.80;

        hg =
            g * 0.80;

        hb =
            b * 0.80;


        sr =
            r * 0.60;

        sg =
            g * 0.60;

        sb =
            b * 0.60;
    }
    else {

        float highlight_strength =
            0.59;

        float shadow_strength =
            0.4375;


        hr =
            r +
            (255 - r) *
            highlight_strength;

        hg =
            g +
            (255 - g) *
            highlight_strength;

        hb =
            b +
            (255 - b) *
            highlight_strength;


        sr =
            r *
            (1 - shadow_strength);

        sg =
            g *
            (1 - shadow_strength);

        sb =
            b *
            (1 - shadow_strength);
    }


    *hi =
        YkAllocRGB(
            dpy,
            hr,
            hg,
            hb
        );


    *sh =
        YkAllocRGB(
            dpy,
            sr,
            sg,
            sb
        );
}


/* ==========================================================
   XRDB loader
   ========================================================== */

static XrmDatabase YkGetResourceDatabase()
{
    FILE *fp;

    char buffer[65536] = {0};

    size_t len;


    fp =
        popen(
            "xrdb -query",
            "r"
        );


    if (!fp) {
        printf(
            "[Yk] failed to execute xrdb\n"
        );

        return NULL;
    }


    len =
        fread(
            buffer,
            1,
            sizeof(buffer) - 1,
            fp
        );


    buffer[len] =
        '\0';


    pclose(fp);


    return XrmGetStringDatabase(
        buffer
    );
}


/* ==========================================================
   Theme building
========================================================== */

static YkTheme YkBuildTheme(
    Display *dpy)
{
    XrmDatabase db;

    XrmValue value;

    char *type;

    YkTheme t;


    memset(
        &t,
        0,
        sizeof(t)
    );


    db =
        YkGetResourceDatabase();


    if (!db)
        return t;


    if (XrmGetResource(
            db,
            "*background",
            "*Background",
            &type,
            &value))
    {
        if (sscanf(
                value.addr,
                "#%lx",
                &t.background) != 1)
        {
            printf(
                "[Yk] warning: could not parse "
                "background '%s' as #rrggbb, "
                "falling back to black\n",
                value.addr
            );

            t.background = 0;
        }
    }


    if (XrmGetResource(
            db,
            "*foreground",
            "*Foreground",
            &type,
            &value))
    {
        if (sscanf(
                value.addr,
                "#%lx",
                &t.foreground) != 1)
        {
            printf(
                "[Yk] warning: could not parse "
                "foreground '%s' as #rrggbb, "
                "falling back to black\n",
                value.addr
            );

            t.foreground = 0;
        }
    }


    if (XrmGetResource(
            db,
            "Yekara*Font",
            "Yekara.Font",
            &type,
            &value))
    {
        char name[128] = {0};
        char size_str[32] = {0};

        char *comma;


        comma =
            strchr(
                value.addr,
                ','
            );


        if (comma) {

            size_t len =
                comma -
                value.addr;


            if (len < sizeof(name)) {

                strncpy(
                    name,
                    value.addr,
                    len
                );


                name[len] =
                    '\0';


                {
                    int end =
                        (int)len - 1;


                    while (
                        end >= 0 &&
                        (
                            name[end] == ' ' ||
                            name[end] == '\t'
                        ))
                    {
                        name[end] =
                            '\0';

                        end--;
                    }
                }


                strncpy(
                    t.font_family,
                    name,
                    sizeof(
                        t.font_family
                    ) - 1
                );


                {
                    char *ptr =
                        comma + 1;


                    while (
                        *ptr == ' ' ||
                        *ptr == '\t')
                    {
                        ptr++;
                    }


                    strncpy(
                        size_str,
                        ptr,
                        sizeof(
                            size_str
                        ) - 1
                    );


                    t.font_size =
                        atoi(
                            size_str
                        );


                    if (t.font_size <= 0)
                        t.font_size = 12;
                }
            }
        }
        else {
            printf(
                "[Yk] warning: "
                "Yekara*Font resource '%s' "
                "has no comma-separated size\n",
                value.addr
            );
        }
    }
    else {
        printf(
            "[Yk] note: no Yekara*Font resource "
            "found, font theme will not be applied\n"
        );
    }


    YkComputeBevels(
        dpy,
        t.background,
        &t.highlight,
        &t.shadow
    );


    t.valid =
        1;


    XrmDestroyDatabase(
        db
    );


    return t;
}


/* ==========================================================
   Apply Color + Font Theme recursively
========================================================== */

static void YkApplyColorsRecursive(
    Widget w,
    YkTheme *t,
    XmRenderTable table)
{
    if (!w ||
        !t ||
        !t->valid)
    {
        return;
    }


    XtPointer user_data = NULL;


    XtVaGetValues(
        w,
        XmNuserData,
        &user_data,
        NULL
    );


    if (
        user_data !=
        YK_SKIP_THEME_FLAG)
    {
        XtVaSetValues(
            w,

            XmNbackground,
            t->background,

            XmNarmColor,
            t->background,

            XmNforeground,
            t->foreground,

            XmNtopShadowColor,
            t->highlight,

            XmNbottomShadowColor,
            t->shadow,

            NULL
        );
    }


    if (
        table &&
        (
            XtIsSubclass(
                w,
                xmPrimitiveWidgetClass
            ) ||

            XtIsSubclass(
                w,
                xmManagerWidgetClass
            ) ||

            XtIsSubclass(
                w,
                xmLabelGadgetClass
            )
        ))
    {
        XtVaSetValues(
            w,
            XmNrenderTable,
            table,
            NULL
        );


        if (
            XtIsSubclass(
                w,
                xmLabelWidgetClass
            ) ||

            XtIsSubclass(
                w,
                xmLabelGadgetClass
            ))
        {
            XmString current =
                NULL;


            XtVaGetValues(
                w,
                XmNlabelString,
                &current,
                NULL
            );


            if (current) {

                XtVaSetValues(
                    w,
                    XmNlabelString,
                    current,
                    NULL
                );


                XmStringFree(
                    current
                );
            }
        }


        else if (
            XtIsSubclass(
                w,
                xmTextWidgetClass
            ) ||

            XtIsSubclass(
                w,
                xmTextFieldWidgetClass
            ))
        {
            XtVaSetValues(
                w,
                XmNtextRenderTable,
                table,
                NULL
            );


            char *current_txt =
                NULL;


            XtVaGetValues(
                w,
                XmNvalue,
                &current_txt,
                NULL
            );


            if (current_txt) {

                XtVaSetValues(
                    w,
                    XmNvalue,
                    current_txt,
                    NULL
                );


                XtFree(
                    current_txt
                );
            }
        }


#ifdef YK_DEBUG_RENDERTABLE
        printf(
            "[Yk] visiting %s, isGadget=%d\n",
            XtName(w),
            XmIsGadget(w)
        );
#endif
    }


    if (XtIsComposite(w)) {

        Widget *children;

        Cardinal count;
        Cardinal i;


        XtVaGetValues(
            w,
            XmNchildren,
            &children,
            XmNnumChildren,
            &count,
            NULL
        );


        for (
            i = 0;
            i < count;
            i++)
        {
            YkApplyColorsRecursive(
                children[i],
                t,
                table
            );
        }
    }


    if (
        XtIsSubclass(
            w,
            xmCascadeButtonWidgetClass
        ) ||

        XtIsSubclass(
            w,
            xmCascadeButtonGadgetClass
        ))
    {
        Widget submenu =
            NULL;


        XtVaGetValues(
            w,
            XmNsubMenuId,
            &submenu,
            NULL
        );


        if (submenu) {

            YkApplyColorsRecursive(
                submenu,
                t,
                table
            );
        }
    }
}


/* ----------------------------------------------------------
   Apply the current theme to a single (possibly newly
   discovered) top-level X window, if Xt already has a Widget
   for it.

   At CreateNotify time Xt frequently hasn't finished building
   its widget tree for a brand-new shell yet, so
   XtWindowToWidget() can legitimately return NULL even though
   the X window already exists. Callers are expected to retry
   at the next observation point (MapNotify) rather than treat
   a NULL result here as an error.
---------------------------------------------------------- */

static void YkApplyThemeToWindow(
    Display *dpy,
    Window window)
{
    Widget widget;


    if (!dpy || window == None)
        return;

    if (!YkCurrentTheme.valid)
        return;


    widget =
        XtWindowToWidget(
            dpy,
            window
        );


    if (!widget)
        return;


    YkApplyColorsRecursive(
        widget,
        &YkCurrentTheme,
        YkGlobalRenderTable
    );


    XmUpdateDisplay(
        widget
    );
}


/* ----------------------------------------------------------
   Register an X11 transient
---------------------------------------------------------- */

static void YkRegisterTransient(
    Display *dpy,
    Window window,
    Window parent)
{
    XWindowAttributes attr;

    YkTransientInfo *info;


    if (!dpy ||
        window == None ||
        parent == None)
    {
        return;
    }


    /*
     * Ignore the application itself.
     */
    if (window == parent)
        return;


    /*
     * Don't register twice.
     */
    if (YkFindTransient(window))
        return;


    /*
     * Obtain its current geometry.
     */
    if (!XGetWindowAttributes(
            dpy,
            window,
            &attr))
    {
        return;
    }


    /*
     * Ignore windows that aren't top-level children of
     * the root.
     *
     * Motif dialog shells are normally in this category.
     */
    if (attr.root != YkRootWindow)
        return;


    info =
        calloc(
            1,
            sizeof(YkTransientInfo)
        );


    if (!info)
        return;


    info->window =
        window;

    info->parent =
        parent;

    info->width =
        attr.width;

    info->height =
        attr.height;


    /*
     * Add to registry.
     */
    info->next =
        YkTransients;

    YkTransients =
        info;


    /*
     * This transient is now itself a valid parent for further
     * nested dialogs (a dialog spawned from a dialog), and its
     * WM_TRANSIENT_FOR will be checked against this set.
     */
    YkAddKnownWindow(
        window
    );


    /*
     * Select geometry events on the actual transient.
     *
     * StructureNotifyMask is local to this window, so it
     * does not interfere with the rest of the application.
     */
    XSelectInput(
        dpy,
        window,
        StructureNotifyMask
    );





    /*
     * Apply the current theme immediately, if Xt already has
     * a Widget for this window. If not (still under
     * construction), YkCheckWindowForTransient() will retry
     * this at the next observation point (MapNotify).
     */
    YkApplyThemeToWindow(
        dpy,
        window
    );


    XFlush(dpy);
}


/* ----------------------------------------------------------
   Check WM_TRANSIENT_FOR
---------------------------------------------------------- */

static void YkCheckWindowForTransient(
    Display *dpy,
    Window window)
{
    Atom actual_type;

    int actual_format;

    unsigned long nitems;
    unsigned long bytes_after;

    unsigned char *data;

    Atom transient_atom;

    Window parent;


    if (!dpy ||
        window == None)
    {
        return;
    }


    /*
     * Never re-register a window that's already known to us
     * (a top-level shell, or a transient we've already seen)
     * as though it were a fresh transient of itself.
     */
    if (YkIsKnownWindow(window))
        return;


    /*
     * WM_TRANSIENT_FOR identifies the logical owner of a
     * top-level transient window.
     */
    transient_atom =
        XInternAtom(
            dpy,
            "WM_TRANSIENT_FOR",
            False
        );


    data = NULL;


    if (XGetWindowProperty(
            dpy,
            window,
            transient_atom,
            0,
            1,
            False,
            XA_WINDOW,
            &actual_type,
            &actual_format,
            &nitems,
            &bytes_after,
            &data) != Success)
    {
        return;
    }


    if (!data)
        return;


    if (actual_type != XA_WINDOW ||
        actual_format != 32 ||
        nitems < 1)
    {
        XFree(data);
        return;
    }


    parent =
        *(Window *)data;


    XFree(data);


    /*
     * Only handle transients belonging to a window libyk
     * already knows about -- a registered top-level shell, or
     * a transient dialog of one (which makes nested dialogs
     * work automatically, with no manual registration call).
     */
    if (!YkIsKnownWindow(parent))
        return;


    YkRegisterTransient(
        dpy,
        window,
        parent
    );


    /*
     * Try to style it now too. If YkRegisterTransient() just
     * ran, this is a harmless repeat of what it already tried;
     * if the Widget wasn't ready at CreateNotify time and this
     * call is happening from MapNotify, this is the retry that
     * actually gets it styled.
     */
    YkApplyThemeToWindow(
        dpy,
        window
    );
}


/* ----------------------------------------------------------
   Root CreateNotify observer
---------------------------------------------------------- */

static void YkObserveCreateNotify(
    XEvent *event)
{
    Display *dpy;


    if (!event ||
        event->type != CreateNotify)
    {
        return;
    }


    if (event->xcreatewindow.parent !=
        YkRootWindow)
    {
        return;
    }


    dpy =
        event->xcreatewindow.display;


    /*
     * The WM_TRANSIENT_FOR property may not exist yet at
     * CreateNotify time because Motif may set it immediately
     * afterward.
     *
     * Therefore this is only an early opportunity to inspect
     * the window.
     */
    YkCheckWindowForTransient(
        dpy,
        event->xcreatewindow.window
    );
}


/* ----------------------------------------------------------
   Root MapNotify observer
---------------------------------------------------------- */

static void YkObserveMapNotify(
    XEvent *event)
{
    Display *dpy;


    if (!event ||
        event->type != MapNotify)
    {
        return;
    }


    /*
     * MapNotify is the important second chance.
     *
     * By this point Motif has normally finished constructing
     * the dialog shell and WM_TRANSIENT_FOR is available, and
     * Xt has normally finished building the Widget for it too.
     */
    dpy =
        event->xmap.display;


    YkCheckWindowForTransient(
        dpy,
        event->xmap.window
    );
}


/* ----------------------------------------------------------
   CreateNotify Xt dispatcher
---------------------------------------------------------- */

static Boolean YkCreateDispatcher(
    XEvent *event)
{
    Boolean result = False;


    /*
     * Observe the event ourselves.
     */
    YkObserveCreateNotify(
        event
    );


    /*
     * Let the original Xt dispatcher continue handling it.
     */
    if (YkOldCreateDispatcher)
        result =
            YkOldCreateDispatcher(
                event
            );


    return result;
}


/* ----------------------------------------------------------
   MapNotify Xt dispatcher
---------------------------------------------------------- */

static Boolean YkMapDispatcher(
    XEvent *event)
{
    Boolean result = False;


    /*
     * Observe the event ourselves.
     */
    YkObserveMapNotify(
        event
    );


    /*
     * Let normal Xt processing continue.
     */
    if (YkOldMapDispatcher)
        result =
            YkOldMapDispatcher(
                event
            );


    return result;
}


/* ----------------------------------------------------------
   ConfigureNotify Xt dispatcher

   Drives dialog recentering / size restoration whenever a
   registered transient's geometry changes. Previously this
   was only wired up through an XtEventHandler that was never
   actually attached to anything (dead code) -- routed through
   XtSetEventDispatcher() instead, the same mechanism already
   used for CreateNotify/MapNotify above, so it actually fires.
---------------------------------------------------------- */

static Boolean YkConfigureDispatcher(
    XEvent *event)
{
    Boolean result = False;


    if (event &&
        event->type == ConfigureNotify)
    {
        YkHandleTransientConfigure(
            event->xconfigure.display,
            event->xconfigure.window
        );
    }


    if (YkOldConfigureDispatcher)
        result =
            YkOldConfigureDispatcher(
                event
            );


    return result;
}


/* ----------------------------------------------------------
   DestroyNotify Xt dispatcher

   Cleans up transient/known-window bookkeeping when a
   registered dialog goes away, so its window ID can't be
   mistaken for a live transient (or a valid transient parent)
   if the X server later reuses it.
---------------------------------------------------------- */

static Boolean YkDestroyDispatcher(
    XEvent *event)
{
    Boolean result = False;


    if (event &&
        event->type == DestroyNotify)
    {
        YkRemoveTransient(
            event->xdestroywindow.window
        );
    }


    if (YkOldDestroyDispatcher)
        result =
            YkOldDestroyDispatcher(
                event
            );


    return result;
}


/* ==========================================================
   Reload resources
========================================================== */

void YkReloadResources(
    Widget winTopLevel)
{
    Display *dpy;

    XmRenderTable global_table =
        NULL;


    if (!winTopLevel)
        return;


    dpy =
        XtDisplay(
            winTopLevel
        );


    YkCurrentTheme =
        YkBuildTheme(
            dpy
        );


    if (
        YkCurrentTheme.font_family[0]
        != '\0')
    {
        Arg args[6];

        int n = 0;


        XtSetArg(
            args[n],
            XmNfontType,
            XmFONT_IS_XFT
        );

        n++;


        XtSetArg(
            args[n],
            XmNfontName,
            YkCurrentTheme.font_family
        );

        n++;


        XtSetArg(
            args[n],
            XmNfontStyle,
            "regular"
        );

        n++;


        XtSetArg(
            args[n],
            XmNfontSize,
            YkCurrentTheme.font_size
        );

        n++;


        XtSetArg(
            args[n],
            XmNrenditionForeground,
            YkCurrentTheme.foreground
        );

        n++;


        XmRendition rendDefault =
            XmRenditionCreate(
                winTopLevel,
                XmFONTLIST_DEFAULT_TAG,
                args,
                n
            );


        XmRendition rendLocale =
            XmRenditionCreate(
                winTopLevel,
                _MOTIF_DEFAULT_LOCALE_TAG,
                args,
                n
            );


        if (
            rendDefault &&
            rendLocale)
        {
            XmRendition rends[2] = {
                rendDefault,
                rendLocale
            };


            global_table =
                XmRenderTableAddRenditions(
                    NULL,
                    rends,
                    2,
                    XmMERGE_REPLACE
                );


            XmRenditionFree(
                rendDefault
            );


            XmRenditionFree(
                rendLocale
            );


            if (!global_table) {
                printf(
                    "[Yk] warning: "
                    "XmRenderTableAddRenditions "
                    "failed\n"
                );
            }
        }
        else {
            printf(
                "[Yk] warning: "
                "XmRenditionCreate failed "
                "for family '%s', size %d\n",

                YkCurrentTheme.font_family,
                YkCurrentTheme.font_size
            );
        }
    }


    YkApplyColorsRecursive(
        winTopLevel,
        &YkCurrentTheme,
        global_table
    );


    /*
     * Cache the render table (rather than freeing it here)
     * so that transients discovered later -- via
     * YkApplyThemeToWindow() -- can be styled with the same
     * table without needing another full reload to happen
     * first. The previous cached table is now safe to free,
     * since YkApplyColorsRecursive() above has already moved
     * every currently-known widget over to the new one.
     */
    if (YkGlobalRenderTable)
        XmRenderTableFree(
            YkGlobalRenderTable
        );

    YkGlobalRenderTable =
        global_table;


    XmUpdateDisplay(
        winTopLevel
    );
}


/* ==========================================================
   Resource IPC handler
========================================================== */

void YkPropertyHandler(
    Widget w,
    XtPointer client_data,
    XEvent *event,
    Boolean *cont)
{
    Widget targetWidget;


    (void)client_data;


    *cont =
        True;


    /*
     * This handler is deliberately ONLY responsible for
     * resource reload messages.
     */
    if (
        event->type == ClientMessage &&
        event->xclient.message_type ==
            YkResourceAtom)
    {
        targetWidget =
            XtWindowToWidget(
                event->xclient.display,
                event->xclient.window
            );


        YkReloadResources(
            targetWidget ?
                targetWidget :
                w
        );
    }
}


/* ==========================================================
   Resource system initialization

   May be called once per top-level shell the application
   creates (a main window, a separate toolbox/palette shell,
   etc). Each call registers that shell as a known window;
   the dispatcher chain itself is only installed the first
   time this function runs, so repeat calls don't stack
   duplicate wrappers around each other.
   ========================================================== */

void YkResourceSysInit(
    Widget winTopLevel)
{
    Display *dpy;

    Window root;

    Window appWindow;


    if (!winTopLevel)
        return;


    dpy =
        XtDisplay(
            winTopLevel
        );


    /*
     * The application shell must be realized because its
     * X window is needed as the WM_TRANSIENT_FOR target.
     */
    if (!XtIsRealized(winTopLevel))
        XtRealizeWidget(
            winTopLevel
        );


    appWindow =
        XtWindow(
            winTopLevel
        );


    if (appWindow == None) {

        printf(
            "[Yk] warning: top-level has no X window\n"
        );

        return;
    }


    YkAddKnownWindow(
        appWindow
    );


    /*
     * Resource reload atom.
     */
    YkResourceAtom =
        XInternAtom(
            dpy,
            "_YK_RESOURCE_SYSTEM",
            False
        );


    /*
     * Find root.
     */
    root =
        DefaultRootWindow(
            dpy
        );


    YkRootWindow =
        root;


    /*
     * Listen for resource reload ClientMessages.
     */
    XtAddEventHandler(
        winTopLevel,
        NoEventMask,
        True,
        YkPropertyHandler,
        NULL
    );


    /*
     * Ask the X server to tell this client when top-level
     * children of the root are created, mapped, or destroyed.
     *
     * IMPORTANT:
     *
     * This is SubstructureNotifyMask, NOT
     * SubstructureRedirectMask.
     *
     * We are observing the window hierarchy, not acting as
     * the window manager.
     */
    XSelectInput(
        dpy,
        root,
        SubstructureNotifyMask
    );


    /*
     * Install our event observers.
     *
     * XtSetEventDispatcher() lets us observe these events
     * without manually pulling events out of Xt's queue.
     *
     * Only done once: a second (or third...) call to
     * YkResourceSysInit(), for an additional top-level shell,
     * must not wrap another layer of dispatcher around the
     * ones already installed.
     */
    if (!YkDispatchersInstalled) {

        YkOldCreateDispatcher =
            XtSetEventDispatcher(
                dpy,
                CreateNotify,
                YkCreateDispatcher
            );


        YkOldMapDispatcher =
            XtSetEventDispatcher(
                dpy,
                MapNotify,
                YkMapDispatcher
            );


        YkOldConfigureDispatcher =
            XtSetEventDispatcher(
                dpy,
                ConfigureNotify,
                YkConfigureDispatcher
            );


        YkOldDestroyDispatcher =
            XtSetEventDispatcher(
                dpy,
                DestroyNotify,
                YkDestroyDispatcher
            );


        YkDispatchersInstalled =
            1;
    }


    /*
     * Flush the setup.
     */
    XFlush(
        dpy
    );
}


/* ==========================================================
   Send resource reload event
========================================================== */

void YkSendReloadEvent(
    Display *display)
{
    XClientMessageEvent ev;

    Window root;

    Window root_return;
    Window parent_return;

    Window *children =
        NULL;

    unsigned int num_children =
        0;


    if (!display)
        return;


    memset(
        &ev,
        0,
        sizeof(ev)
    );


    root =
        DefaultRootWindow(
            display
        );


    ev.type =
        ClientMessage;

    ev.display =
        display;

    ev.message_type =
        YkResourceAtom;

    ev.format =
        32;


    if (
        XQueryTree(
            display,
            root,
            &root_return,
            &parent_return,
            &children,
            &num_children))
    {
        unsigned int i;


        for (
            i = 0;
            i < num_children;
            i++)
        {
            ev.window =
                children[i];


            XSendEvent(
                display,
                children[i],
                False,
                NoEventMask,
                (XEvent *)&ev
            );
        }


        if (children)
            XFree(
                children
            );
    }


    XFlush(
        display
    );
}
