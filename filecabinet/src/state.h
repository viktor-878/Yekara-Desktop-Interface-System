#ifndef FM_STATE_H
#define FM_STATE_H

#include <limits.h>
#include <Xm/Xm.h>
#include "types.h"

/* Current directory */
extern char current_path[PATH_MAX];

/* Widgets shared across modules */
extern Widget toplevel_widget;
extern Widget breadcrumbRow;
extern Widget gridContainer;
extern Widget canvas_popup_menu_w;
extern Widget item_popup_menu;
extern Widget statusLabel;
extern Widget scrolledWindowFrame;

/* Selection */
extern char selected_node_name[256];

/* Cut/copy clipboard */
extern char    source_move_path[PATH_MAX];
extern Boolean is_moving_file;
extern Boolean is_copying_file;

/* View settings */
extern int     last_item_count;
extern Boolean use_icon_view;
extern Boolean show_hidden;

/* Built-in icons */
extern Pixmap iconFolder, iconFolderMask;
extern Pixmap iconFolderBig, iconFolderBigMask;
extern Pixmap iconFile, iconFileMask;
extern Pixmap iconComputer, iconComputerMask;
extern Pixmap iconUp, iconUpMask;

extern Boolean iconFolderValid;
extern Boolean iconFolderBigValid;
extern Boolean iconFileValid;
extern Boolean iconComputerValid;
extern Boolean iconUpValid;

/* Current directory listing */
extern FileItem *g_items;
extern int       g_item_count;

#endif /* FM_STATE_H */
