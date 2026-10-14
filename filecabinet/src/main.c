/*
 * Motif file manager (XmContainer / XmIconGadget version).
 * Uses libYekara's bundle, entry parser, and gzi image systems.
 */

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <Xm/Xm.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>

#include <yk/style.h>
#include <yk/window.h>

#include "icons.h"
#include "nav.h"
#include "state.h"
#include "ui.h"
#include "view.h"

static int (*prev_x_error_handler)(Display *, XErrorEvent *) = NULL;

/* Ignore BadWindow on GetProperty (windows can vanish under us). */
static int tolerant_x_error_handler(Display *dpy, XErrorEvent *ev)
{
    if (ev->error_code == BadWindow && ev->request_code == X_GetProperty)
        return 0;
    return prev_x_error_handler ? prev_x_error_handler(dpy, ev) : 0;
}

int main(int argc, char **argv)
{
    XtAppContext app;
    Widget toplevel;

    if (getcwd(current_path, sizeof(current_path)) == NULL)
        strcpy(current_path, "/");

    {
        char r[PATH_MAX];
        if (realpath(current_path, r))
            snprintf(current_path, sizeof(current_path), "%s", r);
        history_push(current_path);
    }

    signal(SIGCHLD, SIG_IGN);

    toplevel = YkInitWindow(&app, "FileCabinet", &argc, argv);
    toplevel_widget = toplevel;
    prev_x_error_handler = XSetErrorHandler(tolerant_x_error_handler);

    YkResourceSysInit(toplevel);

    XtVaSetValues(toplevel,
        XmNtitle, "File Manager",
        XmNwidth, 760,
        XmNheight, 520,
        NULL);

    build_main_ui(toplevel);

    init_icons(gridContainer);
    render_view();

    XtRealizeWidget(toplevel);
    XtAppMainLoop(app);

    return 0;
}
