#ifndef FM_NAV_H
#define FM_NAV_H

#include <Xm/Xm.h>

/* Records a path in back/forward history (ignores consecutive duplicates). */
void history_push(const char *p);

/* Accepts absolute paths, "..", or a name relative to current_path. */
void change_directory(const char *target_node);

/* Open a folder, launch a .deskapp bundle, or open a file by default app. */
void activate_item(const char *name);

void go_back_cb(Widget w, XtPointer c, XtPointer d);
void go_forward_cb(Widget w, XtPointer c, XtPointer d);
void go_up_cb(Widget w, XtPointer c, XtPointer d);
void go_home_cb(Widget w, XtPointer c, XtPointer d);

#endif /* FM_NAV_H */
