#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xresource.h>
#include <Xm/XmAll.h>
#include <X11/Xatom.h>

#include "yk/style.h"

/* Ensure the layout tag macro is available for modern OpenMotif matching */
#ifndef _MOTIF_DEFAULT_LOCALE_TAG
#define _MOTIF_DEFAULT_LOCALE_TAG "MOTIF_DEFAULT_LOCALE"
#endif

static Atom YkResourceAtom;

/* ----------------------------------------------------------
   Theme structure
---------------------------------------------------------- */
typedef struct {
    Pixel background;
    Pixel foreground;
    Pixel highlight;
    Pixel shadow;
    char font_family[128]; /* OpenMotif Xft expects a clean family string */
    int font_size;         /* OpenMotif Xft expects an explicit integer point size */
    int valid;
} YkTheme;

static YkTheme YkCurrentTheme;

/* ----------------------------------------------------------
   XRDB loader
---------------------------------------------------------- */
static XrmDatabase YkGetResourceDatabase()
{
    FILE *fp;
    char buffer[65536] = {0};
    size_t len;

    fp = popen("xrdb -query", "r");
    if (!fp) {
        printf("[Yk] failed to execute xrdb\n");
        return NULL;
    }

    len = fread(buffer, 1, sizeof(buffer) - 1, fp);
    buffer[len] = '\0';
    pclose(fp);

    return XrmGetStringDatabase(buffer);
}

/* ----------------------------------------------------------
   Color helpers
---------------------------------------------------------- */
static void YkQueryRGB(Display *dpy, Pixel p, int *r, int *g, int *b)
{
    XColor c;
    c.pixel = p;
    XQueryColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &c);
    *r = c.red >> 8;
    *g = c.green >> 8;
    *b = c.blue >> 8;
}

static Pixel YkAllocRGB(Display *dpy, int r, int g, int b)
{
    XColor c;
    Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
    c.red   = r << 8;
    c.green = g << 8;
    c.blue  = b << 8;

    if (XAllocColor(dpy, cmap, &c))
        return c.pixel;

    return BlackPixel(dpy, DefaultScreen(dpy));
}

static void YkComputeBevels(Display *dpy, Pixel bg, Pixel *hi, Pixel *sh)
{
    int r, g, b;
    YkQueryRGB(dpy, bg, &r, &g, &b);

    int hr, hg, hb;
    int sr, sg, sb;
    int brightness = (r * 299 + g * 587 + b * 114) / 10000;

    if (brightness >= 85) {
        hr = r * 0.80; hg = g * 0.80; hb = b * 0.80;
        sr = r * 0.60; sg = g * 0.60; sb = b * 0.60;
    } else {
        float highlight_strength = 0.59;
        float shadow_strength = 0.4375;
        hr = r + (255-r) * highlight_strength;
        hg = g + (255-g) * highlight_strength;
        hb = b + (255-b) * highlight_strength;
        sr = r * (1-shadow_strength);
        sg = g * (1-shadow_strength);
        sb = b * (1-shadow_strength);
    }

    *hi = YkAllocRGB(dpy, hr, hg, hb);
    *sh = YkAllocRGB(dpy, sr, sg, sb);
}

/* ----------------------------------------------------------
   Theme building
---------------------------------------------------------- */
static YkTheme YkBuildTheme(Display *dpy)
{
    XrmDatabase db;
    XrmValue value;
    char *type;
    YkTheme t;

    memset(&t, 0, sizeof(t));
    db = YkGetResourceDatabase();
    if (!db) return t;

    if (XrmGetResource(db, "*background", "*Background", &type, &value)) {
        if (sscanf(value.addr, "#%lx", &t.background) != 1) {
            printf("[Yk] warning: could not parse background '%s' as #rrggbb, falling back to black\n", value.addr);
            t.background = 0;
        }
    }
    if (XrmGetResource(db, "*foreground", "*Foreground", &type, &value)) {
        if (sscanf(value.addr, "#%lx", &t.foreground) != 1) {
            printf("[Yk] warning: could not parse foreground '%s' as #rrggbb, falling back to black\n", value.addr);
            t.foreground = 0;
        }
    }

    if (XrmGetResource(db, "Yekara*Font", "Yekara.Font", &type, &value)) {
        char name[128] = {0};
        char size_str[32] = {0};
        char *comma = strchr(value.addr, ',');

        if (comma) {
            size_t len = comma - value.addr;
            if (len < sizeof(name)) {
                strncpy(name, value.addr, len);
                name[len] = '\0';

                /* Clean up trailing whitespace safely from the font name family string */
                int end = (int)len - 1;
                while (end >= 0 && (name[end] == ' ' || name[end] == '\t')) {
                    name[end] = '\0';
                    end--;
                }
                strncpy(t.font_family, name, sizeof(t.font_family) - 1);

                /* Pull the size string chunk */
                char *ptr = comma + 1;
                while (*ptr == ' ' || *ptr == '\t') ptr++;
                strncpy(size_str, ptr, sizeof(size_str) - 1);
                
                /* Explicit integer conversion for OpenMotif 2.3+ render properties */
                t.font_size = atoi(size_str);
                if (t.font_size <= 0) t.font_size = 12;
            }
        } else {
            printf("[Yk] warning: Yekara*Font resource '%s' has no comma-separated size\n", value.addr);
        }
    } else {
        printf("[Yk] note: no Yekara*Font resource found, font theme will not be applied\n");
    }

    YkComputeBevels(dpy, t.background, &t.highlight, &t.shadow);
    t.valid = 1;

    XrmDestroyDatabase(db);
    return t;
}

/* ----------------------------------------------------------
   Apply Color + Font Theme recursively
---------------------------------------------------------- */
static void YkApplyColorsRecursive(Widget w, YkTheme *t, XmRenderTable table)
{
    if (!w || !t || !t->valid) return;

    XtPointer user_data = NULL;
    XtVaGetValues(w, XmNuserData, &user_data, NULL);

    if (user_data != YK_SKIP_THEME_FLAG) {
        XtVaSetValues(w,
            XmNbackground, t->background,
            XmNarmColor, t->background,
            XmNforeground, t->foreground,
            XmNtopShadowColor, t->highlight,
            XmNbottomShadowColor, t->shadow,
            NULL
        );

        
        }

	/* Apply render tables to Primitive/Manager widgets, Gadgets, and Text components */
        if (table &&
            (XtIsSubclass(w, xmPrimitiveWidgetClass) ||
             XtIsSubclass(w, xmManagerWidgetClass) ||
             XtIsSubclass(w, xmLabelGadgetClass))) {
            
            // Standard widgets use this:
            XtVaSetValues(w, XmNrenderTable, table, NULL);

            /* 1. Force programmatic recalculation for labels and menu triggers */
            if (XtIsSubclass(w, xmLabelWidgetClass) ||
                XtIsSubclass(w, xmLabelGadgetClass)) {
                XmString current = NULL;
                XtVaGetValues(w, XmNlabelString, &current, NULL);
                if (current) {
                    XtVaSetValues(w, XmNlabelString, current, NULL);
                    XmStringFree(current);
                }
            }
            /* 2. CRITICAL FIX FOR EDITABLE TEXT AREA:
                  Text components require XmNtextRenderTable instead! */
            else if (XtIsSubclass(w, xmTextWidgetClass) || 
                     XtIsSubclass(w, xmTextFieldWidgetClass)) {
                
                // Set the text-specific render table here!
                XtVaSetValues(w, XmNtextRenderTable, table, NULL);

                char *current_txt = NULL;
                XtVaGetValues(w, XmNvalue, &current_txt, NULL);
                if (current_txt) {
                    XtVaSetValues(w, XmNvalue, current_txt, NULL);
                    XtFree(current_txt);
                }
            }

#ifdef YK_DEBUG_RENDERTABLE
        printf("[Yk] visiting %s, isGadget=%d\n", XtName(w), XmIsGadget(w));
#endif
    }

    /* Normal composite descent */
    if (XtIsComposite(w)) {
        Widget *children;
        Cardinal count, i;
        XtVaGetValues(w, XmNchildren, &children, XmNnumChildren, &count, NULL);
        for (i = 0; i < count; i++) {
            YkApplyColorsRecursive(children[i], t, table);
        }
    }

    /* Submenu descent (CascadeButton / CascadeButtonGadget popups) */
    if (XtIsSubclass(w, xmCascadeButtonWidgetClass) ||
        XtIsSubclass(w, xmCascadeButtonGadgetClass)) {
        Widget submenu = NULL;
        XtVaGetValues(w, XmNsubMenuId, &submenu, NULL);
        if (submenu) {
            YkApplyColorsRecursive(submenu, t, table);
        }
    }
}

/* ----------------------------------------------------------
   Reload & Global Application Setup
---------------------------------------------------------- */
void YkReloadResources(Widget winTopLevel)
{
    if (!winTopLevel) return;
    Display *dpy = XtDisplay(winTopLevel);

    YkCurrentTheme = YkBuildTheme(dpy);

    XmRenderTable global_table = NULL;

    if (YkCurrentTheme.font_family[0] != '\0') {
        Arg args[6];
        int n = 0;
        
        /* Modern OpenMotif Xft programmatic bindings */
        XtSetArg(args[n], XmNfontType, XmFONT_IS_XFT); n++;
        XtSetArg(args[n], XmNfontName, YkCurrentTheme.font_family); n++;
        XtSetArg(args[n], XmNfontStyle, "regular"); n++;
        XtSetArg(args[n], XmNfontSize, YkCurrentTheme.font_size); n++;
        XtSetArg(args[n], XmNrenditionForeground, YkCurrentTheme.foreground); n++;

        /* Register definitions over both tags to hook local menu streams and fallbacks */
        XmRendition rendDefault = XmRenditionCreate(winTopLevel, XmFONTLIST_DEFAULT_TAG, args, n);
        XmRendition rendLocale  = XmRenditionCreate(winTopLevel, _MOTIF_DEFAULT_LOCALE_TAG, args, n);

        if (rendDefault && rendLocale) {
            XmRendition rends[2] = { rendDefault, rendLocale };
            global_table = XmRenderTableAddRenditions(NULL, rends, 2, XmMERGE_REPLACE);
            
            XmRenditionFree(rendDefault);
            XmRenditionFree(rendLocale);

            if (!global_table) {
                printf("[Yk] warning: XmRenderTableAddRenditions failed\n");
            }
        } else {
            printf("[Yk] warning: XmRenditionCreate failed for family '%s', size %d\n", 
                   YkCurrentTheme.font_family, YkCurrentTheme.font_size);
        }
    }

    /* Iterate layout elements across the tree */
    YkApplyColorsRecursive(winTopLevel, &YkCurrentTheme, global_table);

    if (global_table) {
        XmRenderTableFree(global_table);
    }

    XmUpdateDisplay(winTopLevel);
}

/* ----------------------------------------------------------
   XEvent IPC Management
---------------------------------------------------------- */
void YkPropertyHandler(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    (void)client_data;
    *cont = True;

    if (event->type == ClientMessage && event->xclient.message_type == YkResourceAtom) {
        Widget targetWidget = XtWindowToWidget(event->xclient.display, event->xclient.window);
        YkReloadResources(targetWidget ? targetWidget : w);
    }
}

void YkResourceSysInit(Widget winTopLevel) {
    Display *dpy = XtDisplay(winTopLevel);
    YkResourceAtom = XInternAtom(dpy, "_YK_RESOURCE_SYSTEM", False);

    XtAddEventHandler(winTopLevel, NoEventMask, True, YkPropertyHandler, NULL);
}

void YkSendReloadEvent(Display *display) {
    XClientMessageEvent ev;
    Window root = DefaultRootWindow(display);
    Window root_return, parent_return, *children = NULL;
    unsigned int num_children = 0;

    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.display = display;
    ev.message_type = YkResourceAtom;
    ev.format = 32;

    if (XQueryTree(display, root, &root_return, &parent_return, &children, &num_children)) {
        unsigned int i;
        for (i = 0; i < num_children; i++) {
            ev.window = children[i];
            XSendEvent(display, children[i], False, NoEventMask, (XEvent *)&ev);
        }
        if (children) XFree(children);
    }
    XFlush(display);
}
