#ifndef FM_LABELS_H
#define FM_LABELS_H

#include <stddef.h>
#include <Xm/Xm.h>

/* Font list used for measuring label text (taken from the status label). */
XmFontList label_fontlist(void);

/* Truncates `s` with "..." so it fits in max_w pixels. */
void ellipsize_text(XmFontList fl, const char *s, int max_w,
                    char *out, size_t n);

/* Word-wraps into at most max_lines lines (last line ellipsized). */
void wrap_label(XmFontList fl, const char *text, int max_w,
                int max_lines, char *out, size_t n);

#endif /* FM_LABELS_H */
