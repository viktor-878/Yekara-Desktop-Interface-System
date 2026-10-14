#include <string.h>
#include <stdio.h>

#include "labels.h"
#include "config.h"
#include "state.h"

XmFontList label_fontlist(void)
{
    XmFontList fl = NULL;
    if (statusLabel)
        XtVaGetValues(statusLabel, XmNfontList, &fl, NULL);
    return fl;
}

static int measure_text(XmFontList fl, const char *s, size_t len)
{
    char buf[512];
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';

    XmString xs = XmStringCreateLocalized(buf);
    int w = XmStringWidth(fl, xs);
    XmStringFree(xs);
    return w;
}

/* Byte offsets of each UTF-8 character start; off[n] = end of string. */
static size_t char_offsets(const char *s, size_t *off, size_t max)
{
    size_t n = 0, i = 0, len = strlen(s);

    while (i < len && n < max) {
        off[n++] = i++;
        while (i < len && ((unsigned char)s[i] & 0xC0) == 0x80)
            i++;
    }
    off[n] = i;
    return n;
}

static size_t fit_chars(XmFontList fl, const char *s, const size_t *off,
                        size_t nchars, int max_w)
{
    size_t lo = 0, hi = nchars;

    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (measure_text(fl, s, off[mid]) <= max_w)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

void ellipsize_text(XmFontList fl, const char *s, int max_w,
                    char *out, size_t n)
{
    if (measure_text(fl, s, strlen(s)) <= max_w) {
        snprintf(out, n, "%s", s);
        return;
    }

    size_t off[300];
    size_t nc  = char_offsets(s, off, 299);
    int    ell = measure_text(fl, LABEL_ELLIPSIS, strlen(LABEL_ELLIPSIS));

    size_t lo = 0, hi = nc;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (measure_text(fl, s, off[mid]) + ell <= max_w)
            lo = mid;
        else
            hi = mid - 1;
    }

    snprintf(out, n, "%.*s%s", (int)off[lo], s, LABEL_ELLIPSIS);
}

void wrap_label(XmFontList fl, const char *text, int max_w,
                int max_lines, char *out, size_t n)
{
    char rest[512];
    snprintf(rest, sizeof(rest), "%s", text);

    const char *p = rest;
    out[0] = '\0';

    for (int line = 0; line < max_lines; line++) {
        char piece[512];

        if (measure_text(fl, p, strlen(p)) <= max_w) {
            strncat(out, p, n - strlen(out) - 1);
            return;
        }

        if (line == max_lines - 1) {
            ellipsize_text(fl, p, max_w, piece, sizeof(piece));
            strncat(out, piece, n - strlen(out) - 1);
            return;
        }

        size_t off[300];
        size_t nc = char_offsets(p, off, 299);
        size_t k  = fit_chars(fl, p, off, nc, max_w);
        if (k < 1)
            k = 1;

        size_t cut = off[k];
        for (size_t i = k; i > k / 2 && i > 0; i--) {
            char c = p[off[i] - 1];
            if (c == ' ' || c == '-' || c == '_' || c == '.') {
                cut = off[i];
                break;
            }
        }

        size_t plen = cut;
        if (plen >= sizeof(piece))
            plen = sizeof(piece) - 1;
        memcpy(piece, p, plen);
        piece[plen] = '\0';

        strncat(out, piece, n - strlen(out) - 1);
        strncat(out, "\n",  n - strlen(out) - 1);

        p += cut;
        while (*p == ' ')
            p++;
    }
}
