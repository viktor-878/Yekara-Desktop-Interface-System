#ifndef YKRESOURCE_H
#define YKRESOURCE_H

#include <X11/Intrinsic.h>
#include <Xm/Xm.h>

#define YK_SKIP_THEME_FLAG ((XtPointer)0xDEADBEEF)

void YkResourceSysInit(Widget winTopLevel);

void YkReloadResources(Widget winTopLevel);

void YkSendReloadEvent(Display *display);

#endif
