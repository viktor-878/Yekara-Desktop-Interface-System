#ifndef FM_CALLBACKS_H
#define FM_CALLBACKS_H

#include <Xm/Xm.h>

/* XmContainer callbacks for the file grid and the breadcrumb bar. */
void container_select_cb(Widget w, XtPointer client, XtPointer call);
void container_default_action_cb(Widget w, XtPointer client, XtPointer call);
void container_popup_cb(Widget w, XtPointer client, XtPointer call);
void breadcrumb_select_cb(Widget w, XtPointer client, XtPointer call);

#endif /* FM_CALLBACKS_H */
