#ifndef YKWINDOW_H
#define YKWINDOW_H

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <X11/xpm.h>

Widget YkInitWindow(XtAppContext *app,
    char *id,
    int *argc,
    char **argv);

#endif