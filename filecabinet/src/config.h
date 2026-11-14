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

/* Desktop window (filecabinet --desktop) */
#define DESKTOP_DIR_ENV        "YK_DESKTOP_DIR"    /* overrides ~/Desktop       */
#define DESKTOP_DEFAULT_SUBDIR "Desktop"           /* under $HOME               */
#define DESKTOP_WALLPAPER_ENV  "YK_WALLPAPER"      /* path to an .xpm, tiled    */
#define DESKTOP_COLOR_ENV      "YK_DESKTOP_COLOR"  /* background when no wallp. */
#define DESKTOP_COLOR_DEFAULT  "#008080"
#define DESKTOP_TEXT_COLOR     "white"
#define DESKTOP_ICON_SHADOW    2                   /* bevel around each icon    */
#define DESKTOP_MARGIN         12
#define DESKTOP_CELL_W         (ICON_VIEW_ITEM_WIDTH  + ICON_VIEW_SPACING)
#define DESKTOP_CELL_H         (ICON_VIEW_ITEM_HEIGHT + ICON_VIEW_SPACING)
#define DESKTOP_POLL_MS        2000                /* folder change check       */
/* Remembered icon positions, relative to $HOME */
#define PREFS_SUBDIR           "preferences/file-cabinet"
#define DESKTOP_ENTRY_FILE     "desktop.entry"
#define DESKTOP_COMPUTER_ID    "/"                 /* userData of My Computer;
                                                      no real file can be "/"   */
#define DESKTOP_COMPUTER_LABEL "My Computer"

#endif /* FM_CONFIG_H */
