#ifndef FM_CONFIG_H
#define FM_CONFIG_H

#define MAX_ITEMS 1024

/* Icon archives, relative to the bundle's resources/ directory */
#define ICON_ARCHIVE       "folder.gzi"
#define ICON_ALT_ARCHIVE   "misc.gzi"

/* Breadcrumb bar */
#define URL_ITEM_WIDTH     110
#define URL_ITEM_HEIGHT    96
#define URL_ICON_SIZE      72
#define URL_MARGIN         4

/* Main icon view */
#define ICON_VIEW_ITEM_WIDTH     100
#define ICON_VIEW_ITEM_HEIGHT    90
#define ICON_VIEW_ICON_SIZE      48
#define ICON_VIEW_SPACING        10
#define ICON_VIEW_MARGIN         2
#define VIEW_MARGIN              4

/* Label shaping */
#define ICON_LABEL_MAX_WIDTH        (ICON_VIEW_ITEM_WIDTH - 8)
#define ICON_LABEL_MAX_LINES        2
#define BREADCRUMB_LABEL_MAX_WIDTH  (URL_ITEM_WIDTH - 8)
#define BREADCRUMB_LABEL_MAX_LINES  2
#define LIST_NAME_MAX_WIDTH         260
#define LABEL_ELLIPSIS              "..."

#define HISTORY_MAX 64

#endif /* FM_CONFIG_H */
