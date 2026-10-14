#ifndef FM_MENUS_H
#define FM_MENUS_H

#include <Xm/Xm.h>

/* Builds the File/Edit/View/Go menu bar (attached to the top of `parent`). */
Widget create_menu_bar(Widget parent);

/* Builds the item and canvas context menus; sets item_popup_menu and
 * canvas_popup_menu_w in state. */
void create_popup_menus(Widget parent);

#endif /* FM_MENUS_H */
