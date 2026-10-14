#ifndef FM_DIALOGS_H
#define FM_DIALOGS_H

#include <Xm/Xm.h>

void show_error_dialog(Widget parent, const char *message);
void show_rename_dialog(Widget parent);
void show_new_folder_dialog(Widget parent);
void show_delete_confirm_dialog(Widget parent);
void show_properties_dialog(Widget parent);

#endif /* FM_DIALOGS_H */
