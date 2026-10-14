#ifndef FM_UI_H
#define FM_UI_H

#include <Xm/Xm.h>

/*
 * Builds the whole main window under `toplevel`: menu bar, breadcrumb bar,
 * scrolled file container, status bar and popup menus. Fills in the shared
 * widget globals (breadcrumbRow, gridContainer, statusLabel, ...).
 */
void build_main_ui(Widget toplevel);

#endif /* FM_UI_H */
