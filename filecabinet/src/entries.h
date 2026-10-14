#ifndef FM_ENTRIES_H
#define FM_ENTRIES_H

#include <stddef.h>
#include <Xm/Xm.h>

/* Creates one XmIconGadget; `name` is stored in XmNuserData (strdup'd,
 * freed automatically when the gadget is destroyed). */
Widget create_entry(Widget parent, const char *name,
                    const char *caption,
                    Pixmap icon, Pixmap mask, Boolean icon_valid,
                    unsigned char view_type,
                    Boolean place, int x, int y,
                    const char *detail1, const char *detail2,
                    int label_max_width, int label_max_lines);

/* Copies the name stored in an entry's XmNuserData into `out`. */
void entry_name_of(Widget entry, char *out, size_t n);

void free_payload_destroy_cb(Widget w, XtPointer client_data, XtPointer call_data);

/* Destroys every child of a container (safe against list mutation). */
void destroy_all_children(Widget container);

#endif /* FM_ENTRIES_H */
