/*
 * Desktop Preferences: backdrop color and wallpaper.
 *
 * This is a normal, stand-alone top-level window.  It is deliberately NOT a
 * child of the desktop window: it has its own WM class ("YeKaraPreferences"),
 * no transient-for hint and default decorations, so FVWM decorates and layers
 * it like any other application window instead of treating it as part of the
 * (undecorated, always-on-the-bottom) desktop.
 *
 * Layout:
 *
 *   +-----------------------------------------------------------+
 *   | Preview         | Wallpaper                               |
 *   |  [ miniature ]  |  (None)                                 |
 *   |  [ desktop   ]  |  clouds.xpm                             |
 *   |                 |  ...                                    |
 *   | Layout          |  File: [ ................ ] [Browse...]  |
 *   |  (o) Tile       |                                         |
 *   |  ( ) Center     |                                         |
 *   +-----------------------------------------------------------+
 *   | Backdrop Color                                            |
 *   |  Color: [###] [#4682B4] [Custom...]                       |
 *   |  Presets: [] [] [] [] [] []                               |
 *   +-----------------------------------------------------------+
 *   | [Revert]                          [ OK ] [ Apply ] [Cancel] |
 *   +-----------------------------------------------------------+
 *
 * Everything you change is shown live in the preview; nothing touches the
 * real desktop until you press Apply or OK.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

#include <X11/Xlib.h>
#include <X11/Shell.h>
#include <X11/xpm.h>
#include <X11/Xatom.h>
#include <Xm/Xm.h>
#include <Xm/AtomMgr.h>
#include <Xm/Protocols.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/LabelG.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Scale.h>
#include <Xm/List.h>
#include <Xm/DrawingA.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>
#include <Xm/ToggleBG.h>
#include <Xm/FileSB.h>
#include <Xm/Separator.h>

#include <yk/style.h>

#include "desktop.h"
#include "desktop_prefs.h"
#include "dialogs.h"
#include "state.h"

/* Set to 1 if libyk needs to be told about this window to theme it
 * (it is then called as YkReloadResources(shell), like desktop_build_ui()). */
#define PREFS_CALL_YK_RELOAD 0
#if PREFS_CALL_YK_RELOAD
#include <yk/style.h>
#endif

/* Folders scanned for *.xpm wallpapers ("~" = $HOME).  The folder of the
 * currently configured wallpaper is always scanned too.  Edit to taste. */
static const char *wallpaper_dirs[] = {
    "~/.yekara/wallpapers",
    "/usr/local/share/yekara/wallpapers",
    "/usr/share/yekara/wallpapers",
    "/usr/share/pixmaps",
    NULL
};

#define PREV_W 208          /* preview width in pixels; height follows the screen */

/* ---- widgets / state ----------------------------------------------------- */

static Widget shell, form, wp_text, color_text, swatch, fsb, wp_list;
static Widget preview_da, sliders_box, custom_tog;
static Widget scale_w[3];
static Widget tog_tile, tog_center;

static const struct { const char *name; const char *hex; } presets[] = {
    { "Steel Blue", "#4682B4" },
    { "Teal",       "#008080" },
    { "Slate",      "#6D7B8D" },
    { "Forest",     "#2F5F4F" },
    { "Charcoal",   "#3A3A3A" },
    { "Plum",       "#7B5E8C" },
};
#define NPRESETS ((int)(sizeof(presets) / sizeof(presets[0])))

/* wallpaper list: wp_paths[0] is always "" (= None) */
static char **wp_paths;
static int    wp_count;

/* preview */
static Pixmap        preview_pm;
static GC            preview_gc;
static int           prev_h = 156;
static int           cur_r = 70, cur_g = 130, cur_b = 180;
static unsigned long bg_pix;
static Boolean       bg_pix_valid;
static char          img_path[PATH_MAX * 2];
static XImage       *img, *img_mask;

/* ---- small helpers ------------------------------------------------------- */

static Widget push(Widget parent, const char *name, const char *text,
                   XtCallbackProc cb, XtPointer data)
{
    XmString s = XmStringCreateLocalized((char *)text);
    Widget w = XtVaCreateManagedWidget(name, xmPushButtonWidgetClass, parent,
                                       XmNlabelString, s, NULL);
    XmStringFree(s);
    XtAddCallback(w, XmNactivateCallback, cb, data);
    return w;
}

static Widget label(Widget parent, const char *text)
{
    XmString s = XmStringCreateLocalized((char *)text);
    Widget w = XtVaCreateManagedWidget("lbl", xmLabelGadgetClass, parent,
                                       XmNlabelString, s, NULL);
    XmStringFree(s);
    return w;
}

static Widget hrow(Widget parent)
{
    return XtVaCreateManagedWidget("row", xmRowColumnWidgetClass, parent,
                                   XmNorientation, XmHORIZONTAL,
                                   XmNpacking, XmPACK_TIGHT,
                                   XmNspacing, 6, NULL);
}

static Widget titled_frame(Widget parent, const char *title)
{
    Widget frame = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, parent, NULL);
    XmString s = XmStringCreateLocalized((char *)title);
    XtVaCreateManagedWidget("title", xmLabelGadgetClass, frame,
                            XmNlabelString, s,
                            XmNchildType, XmFRAME_TITLE_CHILD, NULL);
    XmStringFree(s);
    return XtVaCreateManagedWidget("inner", xmRowColumnWidgetClass, frame,
                                   XmNorientation, XmVERTICAL,
                                   XmNmarginWidth, 8, XmNmarginHeight, 8,
                                   XmNspacing, 6, NULL);
}

static void trim_copy(const char *src, char *dst, size_t n)
{
    size_t l;

    while (isspace((unsigned char)*src))
        src++;
    snprintf(dst, n, "%s", src);
    l = strlen(dst);
    while (l > 0 && isspace((unsigned char)dst[l - 1]))
        dst[--l] = '\0';
}

static void get_wp_text(char *out, size_t n)
{
    char *s = XmTextFieldGetString(wp_text);
    trim_copy(s, out, n);
    XtFree(s);
}

static void expand_home(const char *in, char *out, size_t n)
{
    if (in[0] == '~' && (in[1] == '/' || in[1] == '\0')) {
        const char *h = getenv("HOME");
        snprintf(out, n, "%s%s", h ? h : "", in + 1);
    } else {
        snprintf(out, n, "%s", in);
    }
}

static const char *base_of(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static Boolean parse_color(const char *s, int *r, int *g, int *b)
{
    XColor xc;

    if (!s || !*s)
        return False;
    if (!XParseColor(XtDisplay(shell), DefaultColormapOfScreen(XtScreen(shell)), s, &xc))
        return False;
    *r = xc.red >> 8;
    *g = xc.green >> 8;
    *b = xc.blue >> 8;
    return True;
}

/* ---- live preview -------------------------------------------------------- */

static unsigned long alloc_bg_pixel(void)
{
    Display *dpy = XtDisplay(shell);
    Screen  *scr = XtScreen(shell);
    Colormap cm  = DefaultColormapOfScreen(scr);
    XColor   xc;

    if (bg_pix_valid) {
        XFreeColors(dpy, cm, &bg_pix, 1, 0);
        bg_pix_valid = False;
    }
    memset(&xc, 0, sizeof(xc));
    xc.red   = (unsigned short)(cur_r * 257);
    xc.green = (unsigned short)(cur_g * 257);
    xc.blue  = (unsigned short)(cur_b * 257);
    xc.flags = DoRed | DoGreen | DoBlue;
    if (XAllocColor(dpy, cm, &xc)) {
        bg_pix = xc.pixel;
        bg_pix_valid = True;
        return xc.pixel;
    }
    return BlackPixelOfScreen(scr);
}

static void load_preview_image(const char *path)
{
    char full[PATH_MAX * 2];

    if (strcmp(path, img_path) == 0)
        return;                                 /* cached (also failures) */
    snprintf(img_path, sizeof(img_path), "%s", path);

    if (img)      { XDestroyImage(img);      img = NULL; }
    if (img_mask) { XDestroyImage(img_mask); img_mask = NULL; }
    if (!path[0])
        return;

    expand_home(path, full, sizeof(full));
    if (XpmReadFileToImage(XtDisplay(shell), full, &img, &img_mask, NULL) != XpmSuccess) {
        img = NULL;
        img_mask = NULL;
    }
}

static void redraw_preview(void)
{
    if (preview_pm && preview_da && XtIsRealized(preview_da))
        XCopyArea(XtDisplay(shell), preview_pm, XtWindow(preview_da), preview_gc,
                  0, 0, PREV_W, prev_h, 0, 0);
}

static void rebuild_preview(void)
{
    Display *dpy = XtDisplay(shell);
    Screen  *scr = XtScreen(shell);
    char     path[PATH_MAX * 2];
    unsigned long bg;

    if (!preview_pm)
        return;

    get_wp_text(path, sizeof(path));
    load_preview_image(path);

    bg = alloc_bg_pixel();
    XSetForeground(dpy, preview_gc, bg);
    XFillRectangle(dpy, preview_pm, preview_gc, 0, 0, PREV_W, prev_h);

    if (img && img->width > 0 && img->height > 0) {
        XImage *dst = XGetImage(dpy, preview_pm, 0, 0, PREV_W, prev_h,
                                AllPlanes, ZPixmap);
        if (dst) {
            int  sw = WidthOfScreen(scr), sh = HeightOfScreen(scr);
            int  iw = img->width, ih = img->height;
            Boolean center = XmToggleButtonGadgetGetState(tog_center);

            for (int py = 0; py < prev_h; py++) {
                int sy = (int)((long)py * sh / prev_h);
                for (int px = 0; px < PREV_W; px++) {
                    int sx = (int)((long)px * sw / PREV_W);
                    int ix, iy;

                    if (center) {
                        ix = sx - (sw - iw) / 2;
                        iy = sy - (sh - ih) / 2;
                        if (ix < 0 || iy < 0 || ix >= iw || iy >= ih)
                            continue;
                    } else {
                        ix = sx % iw;
                        iy = sy % ih;
                    }
                    if (img_mask && XGetPixel(img_mask, ix, iy) == 0)
                        continue;
                    XPutPixel(dst, px, py, XGetPixel(img, ix, iy));
                }
            }
            XPutImage(dpy, preview_pm, preview_gc, dst, 0, 0, 0, 0, PREV_W, prev_h);
            XDestroyImage(dst);
        }
    }

    /* Sample desktop icons, laid out like the real desktop: a vertical
     * column starting at the top right.  Each caption sits on a beveled
     * plate in the default widget background color. */
    {
        Pixel  ui_bg = 0, fg, top_sh, bot_sh, sel;
        Widget ref = form ? form : shell;
        const int plate_w = 24, plate_h = 7, margin = 6, step = 26;
        int x0 = PREV_W - margin - plate_w;

        XtVaGetValues(ref, XmNbackground, &ui_bg, NULL);
        XmGetColors(scr, DefaultColormapOfScreen(scr), ui_bg,
                    &fg, &top_sh, &bot_sh, &sel);

        for (int i = 0; i < 3; i++) {
            int y  = margin + i * step;
            int ix = x0 + (plate_w - 12) / 2;          /* icon centered over plate */
            int py = y + 13;                           /* plate just below the icon */

            /* icon body */
            XSetForeground(dpy, preview_gc, WhitePixelOfScreen(scr));
            XFillRectangle(dpy, preview_pm, preview_gc, ix, y, 12, 11);
            XSetForeground(dpy, preview_gc, BlackPixelOfScreen(scr));
            XDrawRectangle(dpy, preview_pm, preview_gc, ix, y, 12, 11);

            /* caption plate: fill + raised bevel */
            XSetForeground(dpy, preview_gc, ui_bg);
            XFillRectangle(dpy, preview_pm, preview_gc, x0, py, plate_w, plate_h);
            XSetForeground(dpy, preview_gc, top_sh);
            XDrawLine(dpy, preview_pm, preview_gc, x0, py, x0 + plate_w - 1, py);
            XDrawLine(dpy, preview_pm, preview_gc, x0, py, x0, py + plate_h - 1);
            XSetForeground(dpy, preview_gc, bot_sh);
            XDrawLine(dpy, preview_pm, preview_gc, x0, py + plate_h - 1,
                      x0 + plate_w - 1, py + plate_h - 1);
            XDrawLine(dpy, preview_pm, preview_gc, x0 + plate_w - 1, py,
                      x0 + plate_w - 1, py + plate_h - 1);

            /* fake caption text */
            XSetForeground(dpy, preview_gc, fg);
            XDrawLine(dpy, preview_pm, preview_gc, x0 + 4, py + 3,
                      x0 + plate_w - 5, py + 3);
        }
    }

    redraw_preview();
}

static void preview_expose_cb(Widget w, XtPointer client, XtPointer call)
{
    redraw_preview();
}

/* ---- color --------------------------------------------------------------- */

static void show_color(int r, int g, int b)
{
    char buf[16];

    cur_r = r; cur_g = g; cur_b = b;
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    XmTextFieldSetString(color_text, buf);
    XtVaSetValues(swatch,
                  XtVaTypedArg, XmNbackground, XmRString, buf, (int)strlen(buf) + 1,
                  NULL);
    rebuild_preview();
}

static void set_rgb(int r, int g, int b)
{
    XmScaleSetValue(scale_w[0], r);
    XmScaleSetValue(scale_w[1], g);
    XmScaleSetValue(scale_w[2], b);
    show_color(r, g, b);
}

static void scale_cb(Widget w, XtPointer client, XtPointer call)
{
    int r, g, b;
    XmScaleGetValue(scale_w[0], &r);
    XmScaleGetValue(scale_w[1], &g);
    XmScaleGetValue(scale_w[2], &b);
    show_color(r, g, b);
}

/* client != NULL: silent (focus loss); NULL: report errors (Enter) */
static void color_text_cb(Widget w, XtPointer client, XtPointer call)
{
    int r, g, b;
    char *s = XmTextFieldGetString(color_text);

    if (parse_color(s, &r, &g, &b))
        set_rgb(r, g, b);
    else if (!client) {
        char msg[200];
        snprintf(msg, sizeof(msg), "\"%.150s\" is not a valid color.", s);
        show_error_dialog(shell, msg);
    }
    XtFree(s);
}

static void preset_cb(Widget w, XtPointer client, XtPointer call)
{
    int i = (int)(intptr_t)client, r, g, b;

    if (i >= 0 && i < NPRESETS && parse_color(presets[i].hex, &r, &g, &b))
        set_rgb(r, g, b);
}

static void custom_toggle_cb(Widget w, XtPointer client, XtPointer call)
{
    XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)call;

    if (cbs->set)
        XtManageChild(sliders_box);
    else
        XtUnmanageChild(sliders_box);
}

/* ---- wallpaper list ------------------------------------------------------ */

static void free_paths(void)
{
    for (int i = 0; i < wp_count; i++)
        free(wp_paths[i]);
    free(wp_paths);
    wp_paths = NULL;
    wp_count = 0;
}

static void add_path(const char *path)
{
    char **np;

    for (int i = 0; i < wp_count; i++)
        if (strcmp(wp_paths[i], path) == 0)
            return;
    np = realloc(wp_paths, (size_t)(wp_count + 1) * sizeof(char *));
    if (!np)
        return;
    wp_paths = np;
    wp_paths[wp_count] = strdup(path);
    if (wp_paths[wp_count])
        wp_count++;
}

static void scan_dir(const char *dir_in)
{
    char dir[PATH_MAX];
    DIR *d;
    struct dirent *e;

    expand_home(dir_in, dir, sizeof(dir));
    d = opendir(dir);
    if (!d)
        return;

    while ((e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        char full[PATH_MAX + 300];
        struct stat st;

        if (l < 5 || strcasecmp(e->d_name + l - 4, ".xpm") != 0)
            continue;
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        if (stat(full, &st) == 0 && S_ISREG(st.st_mode))
            add_path(full);
    }
    closedir(d);
}

static int cmp_paths(const void *a, const void *b)
{
    return strcasecmp(base_of(*(char *const *)a), base_of(*(char *const *)b));
}

/* Rebuilds the list and highlights `current` ("" = None). */
static void populate_list(const char *current)
{
    XmString *items;
    int sel = 0, vis = 8, top;
    char cur[PATH_MAX * 2];

    expand_home(current ? current : "", cur, sizeof(cur));

    free_paths();
    add_path("");                               /* (None) */

    if (cur[0]) {
        char dir[PATH_MAX * 2];
        char *slash;

        add_path(current);
        snprintf(dir, sizeof(dir), "%s", cur);
        slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            scan_dir(dir);
        }
    }
    for (int i = 0; wallpaper_dirs[i]; i++)
        scan_dir(wallpaper_dirs[i]);

    if (wp_count > 1)
        qsort(wp_paths + 1, (size_t)(wp_count - 1), sizeof(char *), cmp_paths);

    items = calloc((size_t)wp_count, sizeof(XmString));
    if (!items)
        return;
    for (int i = 0; i < wp_count; i++) {
        items[i] = XmStringCreateLocalized(i == 0 ? "(None)" : (char *)base_of(wp_paths[i]));
        if (current && current[0] && strcmp(wp_paths[i], current) == 0)
            sel = i;
    }

    XmListDeleteAllItems(wp_list);
    XmListAddItems(wp_list, items, wp_count, 0);
    for (int i = 0; i < wp_count; i++)
        XmStringFree(items[i]);
    free(items);

    XmListSelectPos(wp_list, sel + 1, False);
    XtVaGetValues(wp_list, XmNvisibleItemCount, &vis, NULL);
    top = sel + 1 - vis / 2;
    XmListSetPos(wp_list, top < 1 ? 1 : top);
}

static void set_wallpaper(const char *path)
{
    XmTextFieldSetString(wp_text, (char *)path);
    populate_list(path);
    rebuild_preview();
}

static void list_select_cb(Widget w, XtPointer client, XtPointer call)
{
    XmListCallbackStruct *cbs = (XmListCallbackStruct *)call;
    int i = cbs->item_position - 1;

    if (i >= 0 && i < wp_count) {
        XmTextFieldSetString(wp_text, wp_paths[i]);
        rebuild_preview();
    }
}

static void wp_text_cb(Widget w, XtPointer client, XtPointer call)
{
    char path[PATH_MAX * 2];

    get_wp_text(path, sizeof(path));
    set_wallpaper(path);
}

static void layout_cb(Widget w, XtPointer client, XtPointer call)
{
    rebuild_preview();
}

/* ---- file chooser -------------------------------------------------------- */

static void fsb_ok_cb(Widget w, XtPointer client, XtPointer call)
{
    XmFileSelectionBoxCallbackStruct *cbs = (XmFileSelectionBoxCallbackStruct *)call;
    char *s = NULL;

    if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &s) && s) {
        set_wallpaper(s);
        XtFree(s);
    }
    XtUnmanageChild(fsb);
}

static void fsb_cancel_cb(Widget w, XtPointer client, XtPointer call)
{
    XtUnmanageChild(fsb);
}

static void browse_cb(Widget w, XtPointer client, XtPointer call)
{
    if (!fsb) {
        Arg args[4];
        int n = 0;
        XmString pat = XmStringCreateLocalized("*.xpm");

        XtSetArg(args[n], XmNpattern, pat); n++;
        XtSetArg(args[n], XmNautoUnmanage, False); n++;
        fsb = XmCreateFileSelectionDialog(shell, "wallpaperChooser", args, n);
        XmStringFree(pat);

        XtUnmanageChild(XmFileSelectionBoxGetChild(fsb, XmDIALOG_HELP_BUTTON));
        XtAddCallback(fsb, XmNokCallback, fsb_ok_cb, NULL);
        XtAddCallback(fsb, XmNcancelCallback, fsb_cancel_cb, NULL);
    }
    XtManageChild(fsb);
}

/* ---- load / collect / apply ---------------------------------------------- */

static void load_into_dialog(void)
{
    DesktopPrefs p;
    int r, g, b;
    Boolean center;

    desktop_prefs_load(&p);

    center = (strcmp(p.mode, "Center") == 0);
    XmToggleButtonGadgetSetState(tog_tile, !center, False);
    XmToggleButtonGadgetSetState(tog_center, center, False);

    XmTextFieldSetString(wp_text, p.wallpaper);
    populate_list(p.wallpaper);

    if (!parse_color(p.color, &r, &g, &b)) { r = 70; g = 130; b = 180; }
    set_rgb(r, g, b);                           /* also rebuilds the preview */
}

static Boolean collect(DesktopPrefs *p, char *err, size_t en)
{
    int r, g, b;
    char *s = XmTextFieldGetString(color_text);
    char t[PATH_MAX * 2];

    if (!parse_color(s, &r, &g, &b)) {
        snprintf(err, en, "\"%.150s\" is not a valid color.", s);
        XtFree(s);
        return False;
    }
    XtFree(s);
    memset(p, 0, sizeof(*p));
    snprintf(p->color, sizeof(p->color), "#%02X%02X%02X", r, g, b);

    get_wp_text(t, sizeof(t));
    if (strchr(t, ';') || strchr(t, '\n')) {
        snprintf(err, en, "The wallpaper path cannot contain ';'.");
        return False;
    }
    snprintf(p->wallpaper, sizeof(p->wallpaper), "%s", t);

    snprintf(p->mode, sizeof(p->mode), "%s",
             XmToggleButtonGadgetGetState(tog_center) ? "Center" : "Tile");
    return True;
}

static Boolean get_desktop_window(Widget widget, Window *out)
{
    Display *dpy = XtDisplay(widget);
    Screen *scr = XtScreen(widget);
    Window root = RootWindowOfScreen(scr);

    Atom window_atom = XInternAtom(
        dpy,
        YK_DESKTOP_WINDOW,
        False
    );

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    int status = XGetWindowProperty(
        dpy,
        root,
        window_atom,
        0,
        1,
        False,
        XA_WINDOW,
        &actual_type,
        &actual_format,
        &nitems,
        &bytes_after,
        &data
    );

    if (status != Success ||
        actual_type != XA_WINDOW ||
        actual_format != 32 ||
        nitems != 1 ||
        !data) {
        if (data)
            XFree(data);
        return False;
    }

    *out = *(Window *)data;
    XFree(data);

    return True;
}

static Boolean send_apply_message(Widget widget, Window desktop_window)
{
    Display *dpy = XtDisplay(widget);

    XEvent event = {0};

    event.xclient.type = ClientMessage;
    event.xclient.display = dpy;
    event.xclient.window = desktop_window;
    event.xclient.message_type = XInternAtom(
        dpy,
        YK_APPLY_DESKTOP_PREFS,
        False
    );
    event.xclient.format = 32;

    Status result = XSendEvent(
        dpy,
        desktop_window,
        False,
        NoEventMask,
        &event
    );

    XFlush(dpy);

    return result != 0;
}
static Boolean do_apply(void)
{
    DesktopPrefs p;
    char err[PATH_MAX * 2 + 64];
    Window desktop_window;

    /* 1. Collect dialog values */
    if (!collect(&p, err, sizeof(err))) {
        show_error_dialog(shell, err);
        return False;
    }

    /* 2. Check whether the desktop is running */
    if (!get_desktop_window(shell, &desktop_window)) {
        show_error_dialog(shell, "The YeKara desktop is not running.");
        return False;
    }

    /* 3. Save the preferences */
    if (!desktop_prefs_save(&p)) {
        show_error_dialog(shell, "Could not save desktop preferences.");
        return False;
    }

    /* 4. Tell the desktop to reload and apply them */
    if (!send_apply_message(shell, desktop_window)) {
        show_error_dialog(shell, "Could not communicate with the desktop.");
        return False;
    }

    return True;
}

static void close_window(void)
{
    if (fsb && XtIsManaged(fsb))
        XtUnmanageChild(fsb);
    XtPopdown(shell);
}

static void ok_cb(Widget w, XtPointer client, XtPointer call)     { if (do_apply()) close_window(); }
static void apply_cb(Widget w, XtPointer client, XtPointer call)  { do_apply(); }
static void revert_cb(Widget w, XtPointer client, XtPointer call) { load_into_dialog(); }
static void cancel_cb(Widget w, XtPointer client, XtPointer call) { close_window(); }
static void wm_close_cb(Widget w, XtPointer client, XtPointer call) { close_window(); }

/* ---- construction -------------------------------------------------------- */

static void build_dialog(Widget parent)
{
    Display *dpy = XtDisplay(parent);
    Screen  *scr = XtScreen(parent);

    /* Preview height follows the real screen's aspect ratio (4:3 on a CRT). */
    if (WidthOfScreen(scr) > 0)
        prev_h = (int)((long)PREV_W * HeightOfScreen(scr) / WidthOfScreen(scr));
    if (prev_h < 60)  prev_h = 60;
    if (prev_h > 260) prev_h = 260;

    /* Independent top-level shell: own class, not linked to the desktop. */
    shell = XtVaAppCreateShell("desktopPreferences", "YeKaraPreferences",
                               applicationShellWidgetClass, dpy,
                               XmNtitle,             "Desktop Preferences",
                               XmNiconName,          "Preferences",
                               XmNallowShellResize,  False,
                               XmNdeleteResponse,    XmDO_NOTHING,
                               NULL);
    YkResourceSysInit(shell);
    XmAddWMProtocolCallback(shell,
                            XmInternAtom(dpy, "WM_DELETE_WINDOW", False),
                            wm_close_cb, NULL);

    form = XmCreateForm(shell, "desktopPreferencesForm", NULL, 0);

    Widget body = XtVaCreateManagedWidget("body", xmRowColumnWidgetClass, form,
        XmNorientation, XmVERTICAL,
        XmNmarginWidth, 10, XmNmarginHeight, 10, XmNspacing, 8,
        XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
        NULL);

    /* ================= top: preview + wallpaper list ================= */
    Widget top_row = XtVaCreateManagedWidget("topRow", xmRowColumnWidgetClass, body,
        XmNorientation, XmHORIZONTAL, XmNpacking, XmPACK_TIGHT,
        XmNspacing, 8, XmNmarginWidth, 0, XmNmarginHeight, 0, NULL);

    /* ---- left column: preview + layout ---- */
    Widget left_col = XtVaCreateManagedWidget("leftCol", xmRowColumnWidgetClass, top_row,
        XmNorientation, XmVERTICAL, XmNspacing, 8,
        XmNmarginWidth, 0, XmNmarginHeight, 0, NULL);

    Widget pin = titled_frame(left_col, "Preview");
    {
        Widget mon = XtVaCreateManagedWidget("monitor", xmFrameWidgetClass, pin,
            XmNshadowType, XmSHADOW_IN, XmNshadowThickness, 3, NULL);
        preview_da = XtVaCreateManagedWidget("preview", xmDrawingAreaWidgetClass, mon,
            XmNwidth, PREV_W, XmNheight, prev_h,
            XmNresizePolicy, XmRESIZE_NONE,
            XmNmarginWidth, 0, XmNmarginHeight, 0, NULL);
        XtAddCallback(preview_da, XmNexposeCallback, preview_expose_cb, NULL);

        preview_pm = XCreatePixmap(dpy, RootWindowOfScreen(scr), PREV_W, prev_h,
                                   DefaultDepthOfScreen(scr));
        preview_gc = XCreateGC(dpy, preview_pm, 0, NULL);
    }

    Widget lin = titled_frame(left_col, "Layout");
    {
        Widget radio = XmCreateRadioBox(lin, "layoutBox", NULL, 0);
        XmString a = XmStringCreateLocalized("Tile");
        XmString b = XmStringCreateLocalized("Center");

        tog_tile   = XtVaCreateManagedWidget("Tile", xmToggleButtonGadgetClass, radio,
                                             XmNlabelString, a, NULL);
        tog_center = XtVaCreateManagedWidget("Center", xmToggleButtonGadgetClass, radio,
                                             XmNlabelString, b, NULL);
        XmStringFree(a);
        XmStringFree(b);
        XtAddCallback(tog_tile,   XmNvalueChangedCallback, layout_cb, NULL);
        XtAddCallback(tog_center, XmNvalueChangedCallback, layout_cb, NULL);
        XtManageChild(radio);
    }

    /* ---- right column: wallpaper list + path ---- */
    Widget win = titled_frame(top_row, "Wallpaper");
    {
        Arg la[6];
        int n = 0;
        Widget row;

        XtSetArg(la[n], XmNvisibleItemCount, 9);                 n++;
        XtSetArg(la[n], XmNselectionPolicy, XmBROWSE_SELECT);    n++;
        XtSetArg(la[n], XmNlistSizePolicy, XmCONSTANT);          n++;
        XtSetArg(la[n], XmNwidth, 250);                          n++;
        wp_list = XmCreateScrolledList(win, "wallpaperList", la, n);
        XtAddCallback(wp_list, XmNbrowseSelectionCallback, list_select_cb, NULL);
        XtManageChild(wp_list);

        label(win, "File:");
        row = hrow(win);
        wp_text = XtVaCreateManagedWidget("wpText", xmTextFieldWidgetClass, row,
                                          XmNcolumns, 22, NULL);
        XtAddCallback(wp_text, XmNactivateCallback, wp_text_cb, NULL);
        push(row, "browse", "Browse...", browse_cb, NULL);
    }

    /* ================= backdrop color ================= */
    Widget cin = titled_frame(body, "Backdrop Color");
    {
        Widget row = hrow(cin);

        label(row, "Color:");
        {
            XmString blank = XmStringCreateLocalized(" ");
            swatch = XtVaCreateManagedWidget("swatch", xmLabelWidgetClass, row,
                                             XmNlabelString, blank,
                                             XmNrecomputeSize, False,
                                             XmNwidth, 48, XmNheight, 22,
                                             XmNshadowThickness, 2, NULL);
            XmStringFree(blank);
        }
        color_text = XtVaCreateManagedWidget("colorText", xmTextFieldWidgetClass, row,
                                             XmNcolumns, 9, NULL);
        XtAddCallback(color_text, XmNactivateCallback, color_text_cb, NULL);
        XtAddCallback(color_text, XmNlosingFocusCallback, color_text_cb, (XtPointer)1);

        {
            XmString t = XmStringCreateLocalized("Custom...");
            custom_tog = XtVaCreateManagedWidget("customToggle", xmToggleButtonWidgetClass, row,
                                                 XmNlabelString, t,
                                                 XmNindicatorOn, False,
                                                 XmNshadowThickness, 2,
                                                 XmNmarginWidth, 8, NULL);
            XmStringFree(t);
            XtAddCallback(custom_tog, XmNvalueChangedCallback, custom_toggle_cb, NULL);
        }

        row = hrow(cin);
        label(row, "Presets:");
        for (int i = 0; i < NPRESETS; i++) {
            XmString blank = XmStringCreateLocalized(" ");
            Widget b = XtVaCreateManagedWidget(presets[i].name, xmPushButtonWidgetClass,
                row, XmNlabelString, blank, XmNrecomputeSize, False,
                XmNwidth, 34, XmNheight, 22,
                XtVaTypedArg, XmNbackground, XmRString, presets[i].hex,
                              (int)strlen(presets[i].hex) + 1,
                NULL);
            XmStringFree(blank);
            XtAddCallback(b, XmNactivateCallback, preset_cb, (XtPointer)(intptr_t)i);
        }

        /* RGB sliders: hidden until "Custom..." is pressed */
        sliders_box = XtVaCreateWidget("sliders", xmRowColumnWidgetClass, cin,
                                       XmNorientation, XmVERTICAL, XmNspacing, 2, NULL);
        {
            static const char *chan[3] = { "Red", "Green", "Blue" };
            for (int i = 0; i < 3; i++) {
                XmString t = XmStringCreateLocalized((char *)chan[i]);
                scale_w[i] = XtVaCreateManagedWidget(chan[i], xmScaleWidgetClass, sliders_box,
                    XmNorientation, XmHORIZONTAL, XmNminimum, 0, XmNmaximum, 255,
                    XmNshowValue, True, XmNtitleString, t, XmNscaleWidth, 300, NULL);
                XmStringFree(t);
                XtAddCallback(scale_w[i], XmNvalueChangedCallback, scale_cb, NULL);
                XtAddCallback(scale_w[i], XmNdragCallback, scale_cb, NULL);
            }
        }
    }

    /* ================= buttons ================= */
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, body, NULL);
    {
        Widget bf = XtVaCreateManagedWidget("buttonForm", xmFormWidgetClass, body, NULL);
        Widget revert = push(bf, "revert", "Revert", revert_cb, NULL);
        Widget grp = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, bf,
            XmNorientation, XmHORIZONTAL, XmNpacking, XmPACK_COLUMN,
            XmNnumColumns, 1, XmNspacing, 8, XmNmarginWidth, 0, XmNmarginHeight, 0,
            XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
            XmNrightAttachment, XmATTACH_FORM, NULL);
        Widget ok = push(grp, "ok", "OK", ok_cb, NULL);
        push(grp, "apply", "Apply", apply_cb, NULL);
        Widget cancel = push(grp, "cancel", "Cancel", cancel_cb, NULL);

        XtVaSetValues(revert,
            XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
            XmNleftAttachment, XmATTACH_FORM, NULL);
        XtVaSetValues(form, XmNdefaultButton, ok, XmNcancelButton, cancel, NULL);
    }

#if PREFS_CALL_YK_RELOAD
    YkReloadResources(shell);
#endif

     XtManageChild(form);

    /* YkResourceSysInit() can leave the shell with a tiny/explicit size, so
     * re-assert the size from the content's preferred geometry. */
    {
        XtWidgetGeometry pref;
        memset(&pref, 0, sizeof(pref));
        XtQueryGeometry(body, NULL, &pref);

        if (pref.width < 2 || pref.height < 2) {      /* fallback */
            Dimension w = 0, h = 0;
            XtVaGetValues(body, XmNwidth, &w, XmNheight, &h, NULL);
            pref.width = w;
            pref.height = h;
        }
        XtVaSetValues(shell,
                      XmNwidth,            pref.width,
                      XmNheight,           pref.height,
                      XmNallowShellResize, True,
                      NULL);
    }
}

void desktop_prefs_dialog(Widget parent)
{
    if (!shell)
        build_dialog(parent);

    load_into_dialog();

    XtPopup(shell, XtGrabNone);
    if (XtIsRealized(shell))
        XRaiseWindow(XtDisplay(shell), XtWindow(shell));    /* already open: bring to front */
}
