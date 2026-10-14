#include "state.h"

char current_path[PATH_MAX] = "/";

Widget toplevel_widget      = NULL;
Widget breadcrumbRow        = NULL;
Widget gridContainer        = NULL;
Widget canvas_popup_menu_w  = NULL;
Widget item_popup_menu      = NULL;
Widget statusLabel          = NULL;
Widget scrolledWindowFrame  = NULL;

char selected_node_name[256] = "";

char    source_move_path[PATH_MAX] = "";
Boolean is_moving_file  = False;
Boolean is_copying_file = False;

int     last_item_count = 0;
Boolean use_icon_view   = False;
Boolean show_hidden     = False;

Pixmap iconFolder, iconFolderMask;
Pixmap iconFolderBig, iconFolderBigMask;
Pixmap iconFile, iconFileMask;
Pixmap iconComputer, iconComputerMask;
Pixmap iconUp, iconUpMask;

Boolean iconFolderValid    = False;
Boolean iconFolderBigValid = False;
Boolean iconFileValid      = False;
Boolean iconComputerValid  = False;
Boolean iconUpValid        = False;

FileItem *g_items      = NULL;
int       g_item_count = 0;
