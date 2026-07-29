#include <Xm/Xm.h>
#include <Xm/MessageB.h>
#include <X11/Shell.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>    
#include <math.h>
#include <yk/style.h>

/* Structure to hold our mode-specific data */
typedef struct {
    char *label;
    char *command;
} SessionAction;

SessionAction current_action = {"Shut down session?", "poweroff"};

void quit_cb(Widget w, XtPointer client_data, XtPointer call_data) {
    XmAnyCallbackStruct *cb = (XmAnyCallbackStruct *)call_data;
    
    /* If the user clicked OK, execute the system command */
    if (cb->reason == XmCR_OK) {
        printf("Executing: %s\n", current_action.command);
        system(current_action.command);
    }
    
    /* Exit the application regardless (Cancel or OK) */
    exit(0);
}

/* Pseudo-random noise function based on coordinates */
double get_noise(int x, int y) {
    double dot = (double)x * 12.9898 + (double)y * 78.233;
    double sn = fmod(dot, 3.141592653589793);
    double val;
    return modf(sin(sn) * 43758.5453123, &val);
}

int main(int argc, char **argv) {
    XtAppContext app;
    Widget toplevel, overlay, dialog;
    Display *dpy;
    int screen, screen_width, screen_height;
    Arg args[20];
    int n;

    /* 1. PARSE ARGUMENTS */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--reboot") == 0) {
            current_action.label = "Reboot the system?";
            current_action.command = "systemctl reboot";
        } else if (strcmp(argv[i], "--logout") == 0) {
            current_action.label = "Log out of session?";
            current_action.command = "pkill -u $USER"; 
        } else if (strcmp(argv[i], "--shutdown") == 0) {
            current_action.label = "Shut down the system?";
            current_action.command = "systemctl poweroff";
        }
    }
    /* 2. INITIALIZE TOOLKIT */
    toplevel = XtVaAppInitialize(&app, "YekaraSession", NULL, 0, &argc, argv, NULL, NULL);
    dpy = XtDisplay(toplevel);
    screen = DefaultScreen(dpy);
    screen_width  = DisplayWidth(dpy, screen);
    screen_height = DisplayHeight(dpy, screen);

    YkResourceSysInit(toplevel);

    /* 3. CAPTURE BASE SCREENSHOT */
    XImage *base_shot = XGetImage(dpy, RootWindow(dpy, screen), 0, 0, 
                                  screen_width, screen_height, AllPlanes, ZPixmap);
    unsigned long black = BlackPixel(dpy, screen);

    /* 4. THE OVERLAY WINDOW (Fullscreen) */
    n = 0;
    XtSetArg(args[n], XmNwidth, screen_width); n++;
    XtSetArg(args[n], XmNheight, screen_height); n++;
    XtSetArg(args[n], XmNx, 0); n++;
    XtSetArg(args[n], XmNy, 0); n++;
    overlay = XtAppCreateShell("overlay", "Overlay", topLevelShellWidgetClass, dpy, args, n);
    /* 5. THE DIALOG */
    XmString msg = XmStringCreateLocalized(current_action.label);
    n = 0;
    XtSetArg(args[n], XmNmessageString, msg); n++;
    XtSetArg(args[n], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL); n++;
    dialog = XmCreateQuestionDialog(overlay, "confirm", args, n);
    XmStringFree(msg);

    XtUnmanageChild(XmMessageBoxGetChild(dialog, XmDIALOG_HELP_BUTTON));
    XtAddCallback(dialog, XmNokCallback, quit_cb, NULL);
    XtAddCallback(dialog, XmNcancelCallback, quit_cb, NULL);

    /* 6. REALIZE & RUN ANIMATION LOOP */
    XtRealizeWidget(overlay);
    YkResourceSysInit(overlay);
    Window ov_win = XtWindow(overlay);

    Pixmap bg_pix = XCreatePixmap(dpy, ov_win, screen_width, screen_height, DefaultDepth(dpy, screen));
    GC gc = XCreateGC(dpy, bg_pix, 0, NULL);
    
    int fade_band_height = 240; /* Height of the moving soft-fade sweep bar */
    int total_steps = 25; 
    int total_distance = screen_height + fade_band_height;

    XImage *frame = XCreateImage(dpy, DefaultVisual(dpy, screen), DefaultDepth(dpy, screen), ZPixmap, 0, malloc(base_shot->bytes_per_line * screen_height), screen_width, screen_height, 32, 0);

    for (int step = 0; step <= total_steps; step++) {
        memcpy(frame->data, base_shot->data, base_shot->bytes_per_line * screen_height);
	
        int lead_edge = (total_distance * step) / total_steps;
        int trail_edge = lead_edge - fade_band_height;

        for (int y = 0; y < screen_height; y++) {
            if (y <= lead_edge) {
                if (y < trail_edge) {
                    /* Zone 1: Behind the tracking bar -> Flat 50% Checkerboard */
                    for (int x = 0; x < screen_width; x++) {
                        if ((x + y) % 2 == 0) {
                            XPutPixel(frame, x, y, black);
                        }
                    }
                } else {
                    /* Zone 2: Inside the tracking bar -> Noise-dissolved Sweep */
                    double local_density = (double)(lead_edge - y) / fade_band_height;

                    for (int x = 0; x < screen_width; x++) {
                        /* Combined conditions using logical AND (&&) */
                        if (((x + y) % 2 == 0) && (get_noise(x, y) < local_density)) {
                            XPutPixel(frame, x, y, black);
                        }
                    }
                }
            }
        }

        /* Update background definitions and push changes to display hardware */
        XPutImage(dpy, bg_pix, gc, frame, 0, 0, 0, 0, screen_width, screen_height);
        XSetWindowBackgroundPixmap(dpy, ov_win, bg_pix);
        XClearWindow(dpy, ov_win);
        XFlush(dpy);



        /* 16ms frame timing (~1 seconds overall transition) */
        usleep(16000); 
    }
    
    XDestroyImage(frame);
    
    /* Lock the final 50% checkerboard state as the permanent background */
    XSetWindowBackgroundPixmap(dpy, ov_win, bg_pix);
    XClearWindow(dpy, ov_win);

    /* 7. SET WM HINTS (EWMH) */
    Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    
    Atom atoms[2] = { fullscreen, above };
    XChangeProperty(dpy, ov_win, state, XA_ATOM, 32, PropModeReplace, (unsigned char *)atoms, 2);
    /* 8. SHOW & INPUT GRAB */
    XtManageChild(dialog);
    XtPopup(overlay, XtGrabNone);

    XGrabPointer(dpy, ov_win, True, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(dpy, ov_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);

    XDestroyImage(base_shot);
    XFreePixmap(dpy, bg_pix);
    XFreeGC(dpy, gc);

    XtAppMainLoop(app);
    return 0;
}
