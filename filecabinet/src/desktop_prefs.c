/*
 * Desktop Preferences dialog: backdrop color and wallpaper.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <X11/Xlib.h>

#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/LabelG.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/Scale.h>
#include <Xm/TextF.h>
#include <Xm/ToggleBG.h>
#include <Xm/FileSB.h>
#include <Xm/Separator.h>

#include "desktop.h"
#include "desktop_prefs.h"
#include "dialogs.h"
#include "state.h"

static Widget dlg, wp_text, color_text, swatch, fsb;
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

/* ---- small helpers ------------------------------------------------------ */

static Widget push(Widget parent, const char *name, const char *label,
                   XtCallbackProc cb, XtPointer data)
{
    XmString s = XmStringCreateLocalized((char *)label);
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

static Boolean parse_color(const char *s, int *r, int *g, int *b)
{
    XColor xc;

    if (!s || !*s)
        return False;
    if (!XParseColor(XtDisplay(dlg), DefaultColormapOfScreen(XtScreen(dlg)), s, &xc))
        return False;
    *r = xc.red >> 8;
    *g = xc.green >> 8;
    *b = xc.blue >> 8;
    return True;
}

static void show_color(int r, int g, int b)
{
    char buf[16];

    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    XmTextFieldSetString(color_text, buf);
    XtVaSetValues(swatch,
                  XtVaTypedArg, XmNbackground, XmRString, buf, (int)strlen(buf) + 1,
                  NULL);
}

static void set_rgb(int r, int g, int b)
{
    XmScaleSetValue(scale_w[0], r);
    XmScaleSetValue(scale_w[1], g);
    XmScaleSetValue(scale_w[2], b);
    show_color(r, g, b);
}

/* ---- callbacks ---------------------------------------------------------- */

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
        snprintf(msg, sizeof(msg), "\"%s\" is not a valid color.", s);
        show_error_dialog(dlg, msg);
    }
    XtFree(s);
}

static void preset_cb(Widget w, XtPointer client, XtPointer call)
{
    int i = (int)(intptr_t)client, r, g, b;

    if (i >= 0 && i < NPRESETS && parse_color(presets[i].hex, &r, &g, &b))
        set_rgb(r, g, b);
}

static void wp_none_cb(Widget w, XtPointer client, XtPointer call)
{
    XmTextFieldSetString(wp_text, "");
}

static void fsb_ok_cb(Widget w, XtPointer client, XtPointer call)
{
    XmFileSelectionBoxCallbackStruct *cbs = (XmFileSelectionBoxCallbackStruct *)call;
    char *s = NULL;

    if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &s) && s) {
        XmTextFieldSetString(wp_text, s);
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
        fsb = XmCreateFileSelectionDialog(dlg, "wallpaperChooser", args, n);
        XmStringFree(pat);

        XtUnmanageChild(XmFileSelectionBoxGetChild(fsb, XmDIALOG_HELP_BUTTON));
        XtAddCallback(fsb, XmNokCallback, fsb_ok_cb, NULL);
        XtAddCallback(fsb, XmNcancelCallback, fsb_cancel_cb, NULL);
    }
    XtManageChild(fsb);
}

static void load_into_dialog(void)
{
    DesktopPrefs p;
    int r, g, b;
    Boolean center;

    desktop_prefs_load(&p);

    XmTextFieldSetString(wp_text, p.wallpaper);
    if (!parse_color(p.color, &r, &g, &b)) { r = 70; g = 130; b = 180; }
    set_rgb(r, g, b);

    center = (strcmp(p.mode, "Center") == 0);
    XmToggleButtonGadgetSetState(tog_tile, !center, False);
    XmToggleButtonGadgetSetState(tog_center, center, False);
}

static Boolean collect(DesktopPrefs *p, char *err, size_t en)
{
    int r, g, b;
    char *s = XmTextFieldGetString(color_text);

    if (!parse_color(s, &r, &g, &b)) {
        snprintf(err, en, "\"%s\" is not a valid color.", s);
        XtFree(s);
        return False;
    }
    XtFree(s);
    memset(p, 0, sizeof(*p));
    snprintf(p->color, sizeof(p->color), "#%02X%02X%02X", r, g, b);

    s = XmTextFieldGetString(wp_text);
    char *t = s;
    while (isspace((unsigned char)*t))
        t++;
    size_t l = strlen(t);
    while (l > 0 && isspace((unsigned char)t[l - 1]))
        t[--l] = '\0';

    if (strchr(t, ';') || strchr(t, '\n')) {
        snprintf(err, en, "The wallpaper path cannot contain ';'.");
        XtFree(s);
        return False;
    }
    snprintf(p->wallpaper, sizeof(p->wallpaper), "%s", t);
    XtFree(s);

    snprintf(p->mode, sizeof(p->mode), "%s",
             XmToggleButtonGadgetGetState(tog_center) ? "Center" : "Tile");
    return True;
}

static Boolean do_apply(void)
{
    DesktopPrefs p;
    char err[PATH_MAX * 2 + 64];

    if (!collect(&p, err, sizeof(err)) ||
        !desktop_prefs_apply(&p, err, sizeof(err))) {
        show_error_dialog(dlg, err);
        return False;
    }
    return True;
}

static void ok_cb(Widget w, XtPointer client, XtPointer call)
{
    if (do_apply())
        XtUnmanageChild(dlg);
}

static void apply_cb(Widget w, XtPointer client, XtPointer call)  { do_apply(); }
static void reset_cb(Widget w, XtPointer client, XtPointer call)  { load_into_dialog(); }
static void cancel_cb(Widget w, XtPointer client, XtPointer call) { XtUnmanageChild(dlg); }

/* ---- construction ------------------------------------------------------- */

static void build_dialog(Widget parent)
{
    Arg args[6];
    int n = 0;
    XmString title = XmStringCreateLocalized("Desktop Preferences");

    XtSetArg(args[n], XmNdialogTitle, title); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;
    XtSetArg(args[n], XmNdeleteResponse, XmUNMAP); n++;
    dlg = XmCreateFormDialog(parent, "desktopPreferences", args, n);
    XmStringFree(title);

    Widget body = XtVaCreateManagedWidget("body", xmRowColumnWidgetClass, dlg,
        XmNorientation, XmVERTICAL,
        XmNmarginWidth, 10, XmNmarginHeight, 10, XmNspacing, 10,
        XmNtopAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment, XmATTACH_FORM, XmNrightAttachment, XmATTACH_FORM,
        NULL);

    /* ---- Backdrop color ---- */
    Widget cin = titled_frame(body, "Backdrop Color");

    Widget row = hrow(cin);
    label(row, "Color:");
    color_text = XtVaCreateManagedWidget("colorText", xmTextFieldWidgetClass, row,
                                         XmNcolumns, 12, NULL);
    XtAddCallback(color_text, XmNactivateCallback, color_text_cb, NULL);
    XtAddCallback(color_text, XmNlosingFocusCallback, color_text_cb, (XtPointer)1);
    swatch = XtVaCreateManagedWidget("swatch", xmLabelWidgetClass, row,
                                     XmNlabelString, XmStringCreateLocalized(" "),
                                     XmNrecomputeSize, False,
                                     XmNwidth, 60, XmNheight, 24,
                                     XmNshadowThickness, 2, NULL);

    static const char *chan[3] = { "Red", "Green", "Blue" };
    for (int i = 0; i < 3; i++) {
        XmString t = XmStringCreateLocalized((char *)chan[i]);
        scale_w[i] = XtVaCreateManagedWidget(chan[i], xmScaleWidgetClass, cin,
            XmNorientation, XmHORIZONTAL, XmNminimum, 0, XmNmaximum, 255,
            XmNshowValue, True, XmNtitleString, t, XmNscaleWidth, 260, NULL);
        XmStringFree(t);
        XtAddCallback(scale_w[i], XmNvalueChangedCallback, scale_cb, NULL);
        XtAddCallback(scale_w[i], XmNdragCallback, scale_cb, NULL);
    }

    row = hrow(cin);
    label(row, "Presets:");
    for (int i = 0; i < NPRESETS; i++) {
        XmString blank = XmStringCreateLocalized(" ");
        Widget b = XtVaCreateManagedWidget(presets[i].name, xmPushButtonWidgetClass,
            row, XmNlabelString, blank, XmNrecomputeSize, False,
            XmNwidth, 36, XmNheight, 24,
            XtVaTypedArg, XmNbackground, XmRString, presets[i].hex,
                          (int)strlen(presets[i].hex) + 1,
            NULL);
        XmStringFree(blank);
        XtAddCallback(b, XmNactivateCallback, preset_cb, (XtPointer)(intptr_t)i);
    }

    /* ---- Wallpaper ---- */
    Widget win = titled_frame(body, "Wallpaper");

    row = hrow(win);
    label(row, "Image (XPM):");
    wp_text = XtVaCreateManagedWidget("wpText", xmTextFieldWidgetClass, row,
                                      XmNcolumns, 34, NULL);
    push(row, "browse", "Browse...", browse_cb, NULL);
    push(row, "none", "None", wp_none_cb, NULL);

    row = hrow(win);
    label(row, "Layout:");
    Widget radio = XmCreateRadioBox(row, "layoutBox", NULL, 0);
    XtVaSetValues(radio, XmNorientation, XmHORIZONTAL, NULL);
    {
        XmString a = XmStringCreateLocalized("Tile");
        XmString b = XmStringCreateLocalized("Center");
        tog_tile   = XtVaCreateManagedWidget("Tile", xmToggleButtonGadgetClass, radio,
                                             XmNlabelString, a, NULL);
        tog_center = XtVaCreateManagedWidget("Center", xmToggleButtonGadgetClass, radio,
                                             XmNlabelString, b, NULL);
        XmStringFree(a);
        XmStringFree(b);
    }
    XtManageChild(radio);

    /* ---- Buttons ---- */
    XtVaCreateManagedWidget("sep", xmSeparatorWidgetClass, body, NULL);
    row = XtVaCreateManagedWidget("buttons", xmRowColumnWidgetClass, body,
        XmNorientation, XmHORIZONTAL, XmNpacking, XmPACK_COLUMN,
        XmNnumColumns, 1, XmNspacing, 8, NULL);
    push(row, "ok", "OK", ok_cb, NULL);
    push(row, "apply", "Apply", apply_cb, NULL);
    push(row, "reset", "Reset", reset_cb, NULL);
    push(row, "cancel", "Cancel", cancel_cb, NULL);
}

void desktop_prefs_dialog(Widget parent)
{
    if (!dlg)
        build_dialog(parent);

    load_into_dialog();
    XtManageChild(dlg);
    XtPopup(XtParent(dlg), XtGrabNone);
}
