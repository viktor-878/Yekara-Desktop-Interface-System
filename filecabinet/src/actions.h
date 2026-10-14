#ifndef FM_ACTIONS_H
#define FM_ACTIONS_H

#include <Xm/Xm.h>

/*
 * Menu / popup callbacks.
 * context_menu_cb's client_data is an action string:
 *   "open" "contents" "rename" "move" "copy" "delete" "properties"
 */
void context_menu_cb(Widget w, XtPointer client_data, XtPointer call_data);
void canvas_menu_cb(Widget w, XtPointer client_data, XtPointer call_data);   /* paste */
void new_folder_cb(Widget w, XtPointer client_data, XtPointer call_data);
void refresh_cb(Widget w, XtPointer client_data, XtPointer call_data);
void toggle_hidden_cb(Widget w, XtPointer c, XtPointer d);
void exit_cb(Widget w, XtPointer client_data, XtPointer call_data);
void icon_view_cb(Widget w, XtPointer client_data, XtPointer call_data);
void list_view_cb(Widget w, XtPointer client_data, XtPointer call_data);

#endif /* FM_ACTIONS_H */
