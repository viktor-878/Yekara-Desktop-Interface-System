#ifndef FM_DESKTOP_H
#define FM_DESKTOP_H

#include <stddef.h>
#include <Xm/Xm.h>
#include "desktop_prefs.h"

/*
 * Desktop window (filecabinet --desktop).
 *
 * Shows the desktop folder as free-placement icons over a wallpaper, plus a
 * built-in "My Computer" icon. The folder is $YK_DESKTOP_DIR if set,
 * otherwise ~/Desktop (created if missing). Icon positions are remembered in
 * ~/preferences/file-cabinet/desktop.entry.
 *
 * While in desktop mode (state.h: desktop_mode) current_path is the desktop
 * folder and never changes; render_view(), activate_item() and
 * change_directory() are redirected here, so the existing dialogs, paste and
 * context menus keep working unchanged.
 */

#define YK_APPLY_DESKTOP_PREFS "_YEKARA_APPLY_DESKTOP_PREFS"
#define YK_DESKTOP_WINDOW     "_YEKARA_DESKTOP_WINDOW"

/* Resolves (and creates) the desktop folder into out. 0 on success; on
 * failure prints a message to stderr and returns -1. */
int  desktop_resolve_dir(char *out, size_t n);
Boolean desktop_prefs_save(const DesktopPrefs *p);
/* Builds the borderless screen-sized window contents under `toplevel` and
 * sets gridContainer. Call before init_icons() and desktop_render(). */
void desktop_build_ui(Widget toplevel);

/* Rescans the desktop folder and rebuilds the icons. */
void desktop_render(void);

/* Shows the window (below everything else) and starts the folder watcher. */
void desktop_realize(Widget toplevel);

/* Double-click / Open on an icon of the desktop folder. */
void desktop_activate(const char *name);

/* Opens a folder in its own File Cabinet window. */
void desktop_open_folder(const char *path);

#endif /* FM_DESKTOP_H */
