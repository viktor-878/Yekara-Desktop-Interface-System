#ifndef FM_ICONS_H
#define FM_ICONS_H

#include <Xm/Xm.h>

/* Loads one tagged image from a .gzi archive into pixmaps. */
void load_icon_gzi(Widget target_widget, const char *gzi_path,
                   const char *array_name, Pixmap *out, Pixmap *out_mask,
                   Boolean *valid);

/* Loads the built-in folder/file/computer/up icons. */
void init_icons(Widget w);

/*
 * Picks the icon for entry `index` of g_items (0 = the ".." entry).
 * Handles folders, app bundles (per-bundle icon cache), and file types.
 */
void resolve_icon(int index, Pixmap *icon, Pixmap *mask,
                  Boolean *valid, unsigned int *w, unsigned int *h);

#endif /* FM_ICONS_H */
