#include <X11/Shell.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <Xm/Xm.h>
#include <Xm/XmAll.h>
#include <Xm/MainW.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/ComboBox.h>
#include <Xm/Separator.h>
#include <Xm/RowColumn.h>
#include <Xm/MwmUtil.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>    
#include <math.h>
#include <yk/style.h>

typedef struct {
    const char *name;
    const char *description;
    const char *command;
} Win2kAction;

Win2kAction actions[] = {
    {"Shut down", "Ends your session and safely turns off your computer power.", "systemctl poweroff"},
    {"Restart", "Ends your session and restarts the system.", "systemctl reboot"},
    {"Log off", "Ends your session, leaving the computer running for the next user.", "pkill -u $USER"}
};

int selected_action_index = 0;
Widget desc_label;

/* Callback when the user picks an action from the drop-down menu */
void selection_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    XmComboBoxCallbackStruct *cb = (XmComboBoxCallbackStruct *)call_data;
    selected_action_index = cb->item_position; /* 0-indexed in XmComboBox */

    if (selected_action_index >= 0 && selected_action_index < 3) {
        XmString desc_str = XmStringCreateLtoR((char *)actions[selected_action_index].description, XmFONTLIST_DEFAULT_TAG);
        XtVaSetValues(desc_label, XmNlabelString, desc_str, NULL);
        XmStringFree(desc_str);
    }
}

/* Callback for button actions */
void action_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    int action_code = (int)(intptr_t)client_data;
    
    if (action_code == 1) { /* OK clicked */
        printf("Executing: %s\n", actions[selected_action_index].command);
        system(actions[selected_action_index].command);
        exit(0);
    } else if (action_code == 0) { /* Cancel clicked */
        exit(0);
    } else if (action_code == 2) { /* Help clicked */
        printf("Help requested.\n");
    }
}

double get_noise(int x, int y) {
    double dot = (double)x * 12.9898 + (double)y * 78.233;
    double sn = fmod(dot, 3.141592653589793);
    double val;
    return modf(sin(sn) * 43758.5453123, &val);
}

int main(int argc, char **argv) {
    XtAppContext app;
    Widget toplevel, overlay, main_window, form, icon, header_label, combo, separator, button_box, ok_btn, cancel_btn, help_btn;
    Display *dpy;
    int screen, screen_width, screen_height;
    Arg args[20];
    int n;

    /* 1. PARSE INITIAL ARGUMENT SELECTION */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--reboot") == 0) {
            selected_action_index = 1;
        } else if (strcmp(argv[i], "--logout") == 0) {
            selected_action_index = 2;
        } else if (strcmp(argv[i], "--shutdown") == 0) {
            selected_action_index = 0;
        }
    }

    /* 2. INITIALIZE TOOLKIT */
    toplevel = XtVaAppInitialize(&app, "YekaraSession", NULL, 0, &argc, argv, NULL, NULL);
    dpy = XtDisplay(toplevel);
    screen = DefaultScreen(dpy);
    screen_width  = DisplayWidth(dpy, screen);
    screen_height = DisplayHeight(dpy, screen);

    /* 3. CAPTURE BASE SCREENSHOT */
    XImage *base_shot = XGetImage(dpy, RootWindow(dpy, screen), 0, 0, 
                                  screen_width, screen_height, AllPlanes, ZPixmap);
    unsigned long black = BlackPixel(dpy, screen);

    /* 4. OVERLAY WINDOW */
    n = 0;
    XtSetArg(args[n], XmNwidth, screen_width); n++;
    XtSetArg(args[n], XmNheight, screen_height); n++;
    overlay = XtAppCreateShell("overlay", "Overlay", xmDialogShellWidgetClass, dpy, args, n);

    /* 5. FIXED SIZE AND CENTERED MAIN WINDOW SHELL */
    int win_width = 410;
    int win_height = 166;
    int win_x = (screen_width - win_width) / 2;
    int win_y = (screen_height - win_height) / 2;

    n = 0;
    XtSetArg(args[n], XmNtitle, "Shut Down Windows"); n++;
    XtSetArg(args[n], XmNx, win_x); n++;
    XtSetArg(args[n], XmNy, win_y); n++;
    XtSetArg(args[n], XmNwidth, win_width); n++;
    XtSetArg(args[n], XmNheight, win_height); n++;
    XtSetArg(args[n], XmNminWidth, win_width); n++;
    XtSetArg(args[n], XmNmaxWidth, win_width); n++;
    XtSetArg(args[n], XmNminHeight, win_height); n++;
    XtSetArg(args[n], XmNmaxHeight, win_height); n++;
    XtSetArg(args[n], XmNmwmDecorations, MWM_DECOR_BORDER); n++;
    XtSetArg(args[n], XmNmwmFunctions, 0); n++;

    main_window = XtAppCreateShell("sessionWindow", "SessionWindow", topLevelShellWidgetClass, dpy, args, n);

    /* Main Form Layout Container */
    form = XmCreateForm(main_window, "dialogForm", NULL, 0);

    /* Window Background matching for Icon Pixmap */
    Pixel bg, fg;
    XtVaGetValues(form, XmNbackground, &bg, XmNforeground, &fg, NULL);
    Pixmap icon_pix = XmGetPixmap(XtScreen(main_window), "xm_computer", fg, bg);
    if (icon_pix == XmUNSPECIFIED_PIXMAP) {
        icon_pix = XmGetPixmap(XtScreen(main_window), "xm_question", fg, bg);
    }

    /* Computer Icon (Top Left) */
    n = 0;
    XtSetArg(args[n], XmNlabelType, XmPIXMAP); n++;
    XtSetArg(args[n], XmNlabelPixmap, icon_pix); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNtopOffset, 15); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftOffset, 15); n++;
    icon = XmCreateLabel(form, "dialogIcon", args, n);
    XtManageChild(icon);

    /* "What do you want the computer to do?" Label */
    XmString header_str = XmStringCreateLocalized("What do you want the computer to do?");
    n = 0;
    XtSetArg(args[n], XmNlabelString, header_str); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNtopOffset, 15); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNleftWidget, icon); n++;
    XtSetArg(args[n], XmNleftOffset, 15); n++;
    header_label = XmCreateLabel(form, "headerLabel", args, n);
    XmStringFree(header_str);
    XtManageChild(header_label);

    /* Drop-down menu (XmComboBox) */
    XmStringTable items = (XmStringTable)XtMalloc(sizeof(XmString) * 3);
    items[0] = XmStringCreateLocalized((char *)actions[0].name);
    items[1] = XmStringCreateLocalized((char *)actions[1].name);
    items[2] = XmStringCreateLocalized((char *)actions[2].name);

    n = 0;
    XtSetArg(args[n], XmNcomboBoxType, XmDROP_DOWN_LIST); n++;
    XtSetArg(args[n], XmNitems, items); n++;
    XtSetArg(args[n], XmNitemCount, 3); n++;
    XtSetArg(args[n], XmNselectedPosition, selected_action_index); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, header_label); n++;
    XtSetArg(args[n], XmNtopOffset, 8); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNleftWidget, icon); n++;
    XtSetArg(args[n], XmNleftOffset, 15); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightOffset, 15); n++;
    combo = XmCreateComboBox(form, "actionCombo", args, n);
    XtAddCallback(combo, XmNselectionCallback, selection_cb, NULL);
    XtManageChild(combo);

    XmStringFree(items[0]);
    XmStringFree(items[1]);
    XmStringFree(items[2]);
    XtFree((char *)items);

    /* Action Description Label */
    XmString desc_str = XmStringCreateLtoR((char *)actions[selected_action_index].description, XmFONTLIST_DEFAULT_TAG);
    n = 0;
    XtSetArg(args[n], XmNlabelString, desc_str); n++;
    XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, combo); n++;
    XtSetArg(args[n], XmNtopOffset, 10); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNleftWidget, icon); n++;
    XtSetArg(args[n], XmNleftOffset, 15); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightOffset, 15); n++;
    desc_label = XmCreateLabel(form, "descLabel", args, n);
    XmStringFree(desc_str);
    XtManageChild(desc_label);

    /* Horizontal Separator Line */
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, desc_label); n++;
    XtSetArg(args[n], XmNtopOffset, 12); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    separator = XmCreateSeparator(form, "dialogSeparator", args, n);
    XtManageChild(separator);

    /* Action Button Bar (OK, Cancel, Help aligned right) */
    n = 0;
    XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
    XtSetArg(args[n], XmNpacking, XmPACK_COLUMN); n++;
    XtSetArg(args[n], XmNspacing, 6); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, separator); n++;
    XtSetArg(args[n], XmNtopOffset, 10); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNbottomOffset, 10); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightOffset, 15); n++;
    button_box = XmCreateRowColumn(form, "buttonBox", args, n);

    ok_btn = XmCreatePushButton(button_box, "  OK  ", NULL, 0);
    XtAddCallback(ok_btn, XmNactivateCallback, action_cb, (XtPointer)1);
    XtManageChild(ok_btn);

    cancel_btn = XmCreatePushButton(button_box, " Cancel ", NULL, 0);
    XtAddCallback(cancel_btn, XmNactivateCallback, action_cb, (XtPointer)0);
    XtManageChild(cancel_btn);

    XtManageChild(button_box);
    XtManageChild(form);

    /* 6. INITIALIZE STYLE DIRECTLY ON THE TOPLEVEL SHELL */
    YkResourceSysInit(main_window);
    YkReloadResources(main_window);

    /* 7. REALIZE WIDGETS */
    XtRealizeWidget(main_window);
    XtRealizeWidget(overlay);
    
    Window ov_win = XtWindow(overlay);
    Window mw_win = XtWindow(main_window);

    /* 8. SET WM HINTS */
    Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);

    Atom atoms[2] = { fullscreen, above };
    XChangeProperty(dpy, ov_win, state, XA_ATOM, 32, PropModeReplace, (unsigned char *)atoms, 2);

    /* 9. SHOW WINDOWS AND GRAB INPUT */
    XtPopup(overlay, XtGrabNone);
    XLowerWindow(dpy, mw_win);
    
    XGrabPointer(dpy, ov_win, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(dpy, ov_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    
    Pixmap bg_pix = XCreatePixmap(dpy, ov_win, screen_width, screen_height, DefaultDepth(dpy, screen));
    GC gc = XCreateGC(dpy, bg_pix, 0, NULL);
    
    int fade_band_height = 320;
    int total_steps = 253;
    int total_distance = screen_height + fade_band_height;

    double *noise_field = malloc(sizeof(double) * (size_t)screen_width * (size_t)screen_height);
    for (int y = 0; y < screen_height; y++) {
        for (int x = 0; x < screen_width; x++) {
            noise_field[(size_t)y * screen_width + x] = get_noise(x, y);
        }
    }

    XImage *frame = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen), ZPixmap, 0, malloc(base_shot->bytes_per_line * screen_height), screen_width, screen_height, 32, 0);

    /* Transition Animation Loop */
    for (int step = 0; step <= total_steps; step++) {
        memcpy(frame->data, base_shot->data, base_shot->bytes_per_line * screen_height);
    
        int lead_edge = (total_distance * step) / total_steps;
        int trail_edge = lead_edge - fade_band_height;

        for (int y = 0; y < screen_height; y++) {
            if (y <= lead_edge) {
                if (y < trail_edge) {
                    for (int x = 0; x < screen_width; x++) {
                        if ((x + y) % 2 == 0) {
                            XPutPixel(frame, x, y, black);
                        }
                    }
                } else {
                    double local_density = (double)(lead_edge - y) / fade_band_height;

                    for (int x = 0; x < screen_width; x++) {
                        if (((x + y) % 2 == 0) && (noise_field[(size_t)y * screen_width + x] < local_density)) {
                            XPutPixel(frame, x, y, black);
                        }
                    }
                }
            }
        }

        XPutImage(dpy, bg_pix, gc, frame, 0, 0, 0, 0, screen_width, screen_height);
        XSetWindowBackgroundPixmap(dpy, ov_win, bg_pix);
        XClearWindow(dpy, ov_win);
        XFlush(dpy);

        while (XtAppPending(app)) {
            XtAppProcessEvent(app, XtIMAll);
        }
    }

    XRaiseWindow(dpy, mw_win);
    XSetInputFocus(
    dpy,
    mw_win,
    RevertToParent,
    CurrentTime
    );
    
    XGrabPointer(dpy, mw_win, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(dpy, mw_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
    
    XDestroyImage(frame);
    free(noise_field);
    
    XSetWindowBackgroundPixmap(dpy, ov_win, bg_pix);
    XClearWindow(dpy, ov_win);

    XDestroyImage(base_shot);
    XFreePixmap(dpy, bg_pix);
    XFreeGC(dpy, gc);

    XtAppMainLoop(app);
    return 0;
}
