/*
 * Desktop preferences (backdrop color / wallpaper) API.
 * Included by desktop.c and desktop_prefs.c.
 */
#ifndef DESKTOP_PREFS_H
#define DESKTOP_PREFS_H

#include <stddef.h>
#include <limits.h>
#include <Xm/Xm.h>

typedef struct {
    char wallpaper[PATH_MAX * 2];   /* "" = none, "~/" allowed */
    char color[64];                 /* "#RRGGBB" or an X color name */
    char mode[16];                  /* "Tile" or "Center" */
} DesktopPrefs;

/* Loads user prefs, falling back to the bundle default, then built-ins. */
void    desktop_prefs_load(DesktopPrefs *p);

/* Applies to the live desktop and saves to desktop.entry [Desktop]. */
Boolean desktop_prefs_apply(const DesktopPrefs *p, char *err, size_t errn);

/* Shows (building on first use) the Desktop Preferences dialog. */
void    desktop_prefs_dialog(Widget parent);

#endif