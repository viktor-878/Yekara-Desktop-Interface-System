/*
 * mterm.c - a minimal terminal emulator using OpenMotif + libvterm
 *
 * Features: menu bar (File/Edit/Help), scrollbar with scrollback history,
 *           mouse-wheel and Shift+PageUp/PageDown scrolling.
 *           sunken frame around the terminal, mouse selection
 *           (drag / double-click word / triple-click line / right-click
 *           extend), X PRIMARY + CLIPBOARD copy & paste.
 *           The top-level window is created with YkInitWindow().
 *
 * Build:  make
 * Run:    ./mterm
 *
 * Design:
 *   - forkpty() starts $SHELL on a pseudo-terminal.
 *   - XtAppAddInput() watches the pty master; bytes read are fed to libvterm.
 *   - libvterm maintains the live screen; lines that scroll off the top are
 *     handed to us (sb_pushline) and stored in a ring buffer.
 *   - The view is "scroll_off" lines back into history (0 = live screen).
 *     The XmScrollBar maps onto  [scrollback lines + screen rows].
 *   - Key presses (XmNinputCallback) are translated into bytes / vterm keys
 *     and written to the pty.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <pty.h>          /* forkpty (link with -lutil) */

#include <Xm/Xm.h>
#include <Xm/MainW.h>
#include <Xm/Frame.h>
#include <Xm/SeparatoG.h>
#include <Xm/Form.h>
#include <Xm/DrawingA.h>
#include <Xm/ScrollBar.h>
#include <Xm/RowColumn.h>
#include <Xm/CascadeB.h>
#include <Xm/PushBG.h>
#include <Xm/MessageB.h>
#include <Xm/SelectioB.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/Xft/Xft.h>
#include <fontconfig/fontconfig.h>

#include <yk/window.h>
#include <vterm.h>

#define DEFAULT_ROWS 24
#define DEFAULT_COLS 80
#define COLOR_CACHE_SIZE 1024
#define SB_MAX 5000            /* max lines of scrollback */
#define WHEEL_LINES 3

/* ---------- global state (it's a small program) ---------- */
static Display       *dpy;
static Widget         toplevel, draw, vsb;
static XtAppContext   app;
static Window         win;
static GC             gc;
static int            cell_w, cell_h, cell_ascent;

/* ---------- Xft font state ---------- */
#define MAX_FALLBACK 12
#define MAX_MISSING  64
static XftFont   *fonts[4];                 /* regular, bold, italic, bold+italic */
static XftFont   *fallback[MAX_FALLBACK];   /* fonts for glyphs the main font lacks */
static int        nfallback;
static uint32_t   missing[MAX_MISSING];     /* codepoints no font has */
static int        missing_pos;
static FcPattern *base_pat;                 /* parsed pattern of the current font */
static char      *font_name;                /* current font as a fontconfig string */
static double     default_size;
static XftDraw   *xftdraw;
static Visual    *vis;
static Colormap   cmap;
static int        scr_num;
static char      *opt_fa;                   /* -fa pattern */
static double     opt_fs;                   /* -fs size    */

typedef struct { String face; } AppRes;
static XtResource app_res[] = {
    { "faceName", "FaceName", XtRString, sizeof(String),
      XtOffsetOf(AppRes, face), XtRString, "Adobe Courier:size=12" },
};

static void change_size(double delta, int reset);   /* defined below */

static VTerm         *vt;
static VTermScreen   *vts;
static int            rows = DEFAULT_ROWS, cols = DEFAULT_COLS;
static int            master_fd = -1;
static pid_t          child_pid = -1;
static VTermPos       cursor_pos;
static int            cursor_visible = 1;
static VTermColor     default_fg, default_bg;


/* ---------- scrollback ring buffer ---------- */
typedef struct { int cols; VTermScreenCell *cells; } SbLine;
static SbLine sb_lines[SB_MAX];
static int    sb_start, sb_count;
static int    scroll_off;        /* how many lines we're scrolled back */
static int    sb_dirty;          /* scrollbar needs updating */
static int    view_dirty;        /* history view needs repainting */
static long   sb_total;          /* lines ever pushed (minus popped): gives
                                  * every line a stable "absolute" number:
                                  * abs = sb_total - scroll_off + view_row */

/* ---------- selection state ---------- */
#define WORD_CHARS "_-./~@%+"
typedef struct { long line; int col; } SelPos;

static int     sel_active;               /* a selection exists */
static SelPos  sel_s, sel_e;             /* normalized, inclusive */
static int     sel_dragging;
static int     sel_mode;                 /* 0 char, 1 word, 2 line */
static SelPos  anchor_lo, anchor_hi;
static int     click_count;
static Time    last_click_time;
static SelPos  last_click_pos;
static int     ptr_x, ptr_y;
static XtIntervalId autoscroll_id;
static char   *primary_text, *clip_text;
static int     reown_guard;
static Atom    a_utf8, a_targets, a_clipboard, a_text;

static SbLine *sb_get(int i) { return &sb_lines[(sb_start + i) % SB_MAX]; }

static void blank_cell(VTermScreenCell *cell)
{
    memset(cell, 0, sizeof *cell);
    cell->width = 1;
    cell->fg = default_fg;
    cell->bg = default_bg;
}

static int cb_sb_pushline(int ncols, const VTermScreenCell *cells, void *user)
{
    (void)user;
    SbLine *l;
    if (sb_count == SB_MAX) {                 /* drop the oldest line */
        l = &sb_lines[sb_start];
        free(l->cells);
        sb_start = (sb_start + 1) % SB_MAX;
    } else {
        l = &sb_lines[(sb_start + sb_count) % SB_MAX];
        sb_count++;
    }
    sb_total++;
    l->cols = ncols;
    l->cells = malloc(sizeof(VTermScreenCell) * (size_t)ncols);
    if (!l->cells) { l->cols = 0; }          /* keep numbering; shows blank */
    else memcpy(l->cells, cells, sizeof(VTermScreenCell) * (size_t)ncols);

    if (scroll_off > 0) {                     /* keep the same text in view */
        scroll_off++;
        if (scroll_off > sb_count) scroll_off = sb_count;
        view_dirty = 1;
    }
    sb_dirty = 1;
    return 1;
}

/* libvterm pulls lines back when the window grows taller */
static int cb_sb_popline(int ncols, VTermScreenCell *cells, void *user)
{
    (void)user;
    if (sb_count == 0) return 0;

    SbLine *l = sb_get(sb_count - 1);
    int n = l->cols < ncols ? l->cols : ncols;
    memcpy(cells, l->cells, sizeof(VTermScreenCell) * (size_t)n);
    for (int i = n; i < ncols; i++) blank_cell(&cells[i]);

    free(l->cells);
    l->cells = NULL;
    sb_count--;
    sb_total--;
    if (scroll_off > sb_count) scroll_off = sb_count;
    sb_dirty = 1;
    return 1;
}

static void clear_scrollback_lines(void)
{
    for (int i = 0; i < sb_count; i++) {
        free(sb_get(i)->cells);
        sb_get(i)->cells = NULL;
    }
    sb_start = sb_count = 0;
    scroll_off = 0;
}

/* ---------- colors ---------- */
static struct { uint32_t rgb; XftColor xc; int valid; }
    color_cache[COLOR_CACHE_SIZE];

static void rgb_of(const VTermColor *c, int *r, int *g, int *b)
{
    VTermColor t = *c;
    vterm_screen_convert_color_to_rgb(vts, &t);     /* palette -> RGB */
    *r = t.rgb.red; *g = t.rgb.green; *b = t.rgb.blue;
}

static XftColor *color_for(const VTermColor *c)
{
    int r, g, b;
    rgb_of(c, &r, &g, &b);
    uint32_t key = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    unsigned idx = (key * 2654435761u) % COLOR_CACHE_SIZE;

    if (color_cache[idx].valid && color_cache[idx].rgb == key)
        return &color_cache[idx].xc;

    XRenderColor rc = { (unsigned short)(r * 257), (unsigned short)(g * 257),
                        (unsigned short)(b * 257), 0xffff };
    if (!XftColorAllocValue(dpy, vis, cmap, &rc, &color_cache[idx].xc)) {
        memset(&color_cache[idx].xc, 0, sizeof color_cache[idx].xc);
        color_cache[idx].xc.pixel = BlackPixel(dpy, scr_num);
    }
    color_cache[idx].rgb = key;
    color_cache[idx].valid = 1;
    return &color_cache[idx].xc;
}

/* ---------- glyph -> font selection (with fontconfig fallback) ---------- */
static XftFont *font_for(uint32_t cp, int bold, int italic,
                         int *synth_bold, int *is_fallback)
{
    int idx = (bold ? 1 : 0) | (italic ? 2 : 0);
    XftFont *f = fonts[idx];
    *synth_bold = 0;
    *is_fallback = 0;

    if (!f && bold) {                       /* no bold face: fake it */
        f = fonts[italic ? 2 : 0];
        if (f) *synth_bold = 1;
    }
    if (!f) f = fonts[0];

    if (cp < 0x80 || XftCharExists(dpy, f, cp)) return f;

    *is_fallback = 1;
    for (int i = 0; i < nfallback; i++)
        if (XftCharExists(dpy, fallback[i], cp)) return fallback[i];
    for (int i = 0; i < MAX_MISSING; i++)
        if (missing[i] == cp) { *is_fallback = 0; return f; }

    if (nfallback < MAX_FALLBACK && base_pat) {
        FcPattern *p = FcPatternDuplicate(base_pat);
        FcCharSet *cs = FcCharSetCreate();
        FcCharSetAddChar(cs, cp);
        FcPatternAddCharSet(p, FC_CHARSET, cs);
        FcCharSetDestroy(cs);
        FcPatternAddBool(p, FC_SCALABLE, FcTrue);

        FcResult res;
        FcPattern *m = XftFontMatch(dpy, scr_num, p, &res);
        FcPatternDestroy(p);
        if (m) {
            XftFont *nf = XftFontOpenPattern(dpy, m);
            if (!nf) FcPatternDestroy(m);
            else if (XftCharExists(dpy, nf, cp)) {
                fallback[nfallback++] = nf;
                return nf;
            } else XftFontClose(dpy, nf);
        }
    }
    missing[missing_pos++ % MAX_MISSING] = cp;   /* don't look again */
    *is_fallback = 0;
    return f;
}

/* ---------- procedurally drawn glyphs ----------
 * Box drawing (U+2500-257F), block elements (U+2580-259F) and Braille
 * (U+2800-28FF) are drawn with rectangles, so lines join pixel-perfectly
 * across cells whatever the font's metrics are. */
static void frect(int x, int y, int w, int h, unsigned long px)
{
    if (w <= 0 || h <= 0) return;
    XSetForeground(dpy, gc, px);
    XFillRectangle(dpy, win, gc, x, y, (unsigned)w, (unsigned)h);
}

/* one digit per arm, in the order left, right, up, down:
 * 0 none, 1 light, 2 heavy, 3 double */
static const char *box_tab[128] = {
    "1100", "2200", "0011", "0022", "1100", "2200", "0011", "0022",
    "1100", "2200", "0011", "0022", "0101", "0201", "0102", "0202",
    "1001", "2001", "1002", "2002", "0110", "0210", "0120", "0220",
    "1010", "2010", "1020", "2020", "0111", "0211", "0121", "0112",
    "0122", "0221", "0212", "0222", "1011", "2011", "1021", "1012",
    "1022", "2021", "2012", "2022", "1101", "2101", "1201", "2201",
    "1102", "2102", "1202", "2202", "1110", "2110", "1210", "2210",
    "1120", "2120", "1220", "2220", "1111", "2111", "1211", "2211",
    "1121", "1112", "1122", "2121", "1221", "2112", "1212", "2221",
    "2212", "2122", "1222", "2222", "1100", "2200", "0011", "0022",
    "3300", "0033", "0301", "0103", "0303", "3001", "1003", "3003",
    "0310", "0130", "0330", "3010", "1030", "3030", "0311", "0133",
    "0333", "3011", "1033", "3033", "3301", "1103", "3303", "3310",
    "1130", "3330", "3311", "1133", "3333", "0101", "1001", "1010",
    "0110", "0000", "0000", "0000", "1000", "0010", "0100", "0001",
    "2000", "0020", "0200", "0002", "1200", "0012", "2100", "0021"
};

typedef struct { int n; int a[2], b[2]; } LineSet;

/* perpendicular extents (relative to the cell centre) of an arm's lines */
static LineSet line_set(int style, int lw, int hw)
{
    LineSet s = { 0, {0, 0}, {0, 0} };
    if (style == 1 || style == 2) {
        int t = style == 1 ? lw : hw;
        s.n = 1; s.a[0] = -(t / 2); s.b[0] = s.a[0] + t;
    } else if (style == 3) {
        int T = 3 * lw, lo = -(T / 2), hi = lo + T;
        s.n = 2;
        s.a[0] = lo;       s.b[0] = lo + lw;
        s.a[1] = hi - lw;  s.b[1] = hi;
    }
    return s;
}

static void draw_box(uint32_t cp, int x, int y, int w, int h, unsigned long fg)
{
    int idx = (int)(cp - 0x2500);

    int lw = h / 12; if (lw < 1) lw = 1;
    int hw = lw * 2; if (hw < 2) hw = 2;

    if (idx >= 0x71 && idx <= 0x73) {                /* diagonals */
        XSetForeground(dpy, gc, fg);
        XSetLineAttributes(dpy, gc, (unsigned)lw, LineSolid, CapButt, JoinMiter);
        if (idx != 0x72) XDrawLine(dpy, win, gc, x + w - 1, y, x, y + h - 1);
        if (idx != 0x71) XDrawLine(dpy, win, gc, x, y, x + w - 1, y + h - 1);
        XSetLineAttributes(dpy, gc, 0, LineSolid, CapButt, JoinMiter);
        return;
    }

    const char *spec = box_tab[idx];
    int st[4];                                       /* l r u d */
    for (int i = 0; i < 4; i++) st[i] = spec[i] - '0';

    int cx = x + w / 2, cy = y + h / 2;
    LineSet S[4];
    for (int i = 0; i < 4; i++) S[i] = line_set(st[i], lw, hw);

    /* extents of all vertical lines (x offsets) and horizontal lines (y) */
    int R_all = 0, L_all = 0, B_all = 0, T_all = 0;
    int have_v = 0, have_h = 0;
    for (int i = 2; i < 4; i++)
        for (int k = 0; k < S[i].n; k++) {
            if (!have_v || S[i].b[k] > R_all) R_all = S[i].b[k];
            if (!have_v || S[i].a[k] < L_all) L_all = S[i].a[k];
            have_v = 1;
        }
    for (int i = 0; i < 2; i++)
        for (int k = 0; k < S[i].n; k++) {
            if (!have_h || S[i].b[k] > B_all) B_all = S[i].b[k];
            if (!have_h || S[i].a[k] < T_all) T_all = S[i].a[k];
            have_h = 1;
        }

    /* horizontal arms: i = 0 left, 1 right */
    for (int i = 0; i < 2; i++) {
        for (int k = 0; k < S[i].n; k++) {
            int ya = cy + S[i].a[k], yb = cy + S[i].b[k];
            int face = -1;                           /* vertical arm it meets */
            if (st[i] == 3) face = (k == 0) ? 2 : 3; /* upper line / lower line */
            int edge;
            if (face >= 0 && st[face] > 0) {         /* interrupted by it */
                edge = (i == 0) ? cx + S[face].b[0]
                                : cx + S[face].a[S[face].n - 1];
            } else {                                 /* runs through */
                edge = (i == 0) ? cx + R_all : cx + L_all;
            }
            if (i == 0) frect(x, ya, edge - x, yb - ya, fg);
            else        frect(edge, ya, x + w - edge, yb - ya, fg);
        }
    }
    /* vertical arms: i = 2 up, 3 down */
    for (int i = 2; i < 4; i++) {
        for (int k = 0; k < S[i].n; k++) {
            int xa = cx + S[i].a[k], xb = cx + S[i].b[k];
            int face = -1;                           /* horizontal arm it meets */
            if (st[i] == 3) face = (k == 0) ? 0 : 1; /* left line / right line */
            int edge;
            if (face >= 0 && st[face] > 0) {
                edge = (i == 2) ? cy + S[face].b[0]
                                : cy + S[face].a[S[face].n - 1];
            } else {
                edge = (i == 2) ? cy + B_all : cy + T_all;
            }
            if (i == 2) frect(xa, y, xb - xa, edge - y, fg);
            else        frect(xa, edge, xb - xa, y + h - edge, fg);
        }
    }
}

static int draw_block(uint32_t cp, int x, int y, int w, int h,
                      unsigned long fg, const VTermColor *fgc,
                      const VTermColor *bgc)
{
#define EIGHTHS(x0, y0, x1, y1) \
    frect(x + w * (x0) / 8, y + h * (y0) / 8, \
          w * (x1) / 8 - w * (x0) / 8, h * (y1) / 8 - h * (y0) / 8, fg)

    if (cp == 0x2580)                    EIGHTHS(0, 0, 8, 4);
    else if (cp >= 0x2581 && cp <= 0x2587) EIGHTHS(0, 8 - (int)(cp - 0x2580), 8, 8);
    else if (cp == 0x2588)               EIGHTHS(0, 0, 8, 8);
    else if (cp >= 0x2589 && cp <= 0x258F) EIGHTHS(0, 0, (int)(0x2590 - cp), 8);
    else if (cp == 0x2590)               EIGHTHS(4, 0, 8, 8);
    else if (cp >= 0x2591 && cp <= 0x2593) {         /* shades: blend fg/bg */
        int a = (int)(cp - 0x2590) * 64;             /* 25%, 50%, 75% */
        int fr, fgn, fb, br, bg2, bb;
        rgb_of(fgc, &fr, &fgn, &fb);
        rgb_of(bgc, &br, &bg2, &bb);
        VTermColor mix;
        vterm_color_rgb(&mix, (uint8_t)((fr * a + br * (256 - a)) / 256),
                              (uint8_t)((fgn * a + bg2 * (256 - a)) / 256),
                              (uint8_t)((fb * a + bb * (256 - a)) / 256));
        frect(x, y, w, h, color_for(&mix)->pixel);
    }
    else if (cp == 0x2594)               EIGHTHS(0, 0, 8, 1);
    else if (cp == 0x2595)               EIGHTHS(7, 0, 8, 8);
    else if (cp >= 0x2596 && cp <= 0x259F) {         /* quadrants */
        static const int q[10] = { 4, 8, 1, 13, 9, 7, 11, 2, 6, 14 };
        int m = q[cp - 0x2596];                      /* 1 UL, 2 UR, 4 LL, 8 LR */
        if (m & 1) EIGHTHS(0, 0, 4, 4);
        if (m & 2) EIGHTHS(4, 0, 8, 4);
        if (m & 4) EIGHTHS(0, 4, 4, 8);
        if (m & 8) EIGHTHS(4, 4, 8, 8);
    }
    else return 0;
    return 1;
#undef EIGHTHS
}

static void draw_braille(uint32_t cp, int x, int y, int w, int h,
                         unsigned long fg)
{
    /* bit -> (column, row) */
    static const int bx[8] = { 0, 0, 0, 1, 1, 1, 0, 1 };
    static const int by[8] = { 0, 1, 2, 0, 1, 2, 3, 3 };
    int ds = w / 4;
    if (h / 8 < ds) ds = h / 8;
    if (ds < 1) ds = 1;
    for (int bit = 0; bit < 8; bit++) {
        if (!((cp - 0x2800) & (1u << bit))) continue;
        int cx = x + w * (2 * bx[bit] + 1) / 4;
        int cy = y + h * (2 * by[bit] + 1) / 8;
        frect(cx - ds / 2, cy - ds / 2, ds, ds, fg);
    }
}

static int draw_special(uint32_t cp, int x, int y, int w, int h,
                        const VTermColor *fgc, const VTermColor *bgc)
{
    if (cp >= 0x2500 && cp <= 0x257F) {
        draw_box(cp, x, y, w, h, color_for(fgc)->pixel);
        return 1;
    }
    if (cp >= 0x2580 && cp <= 0x259F)
        return draw_block(cp, x, y, w, h, color_for(fgc)->pixel, fgc, bgc);
    if (cp >= 0x2800 && cp <= 0x28FF) {
        draw_braille(cp, x, y, w, h, color_for(fgc)->pixel);
        return 1;
    }
    return 0;
}

/* ---------- drawing ---------- */

/* Fetch the cell shown at view position (row, col), taking scrollback
 * into account. */
static void get_view_cell(int row, int col, VTermScreenCell *cell)
{
    if (row < scroll_off) {
        SbLine *l = sb_get(sb_count - scroll_off + row);
        if (col < l->cols) *cell = l->cells[col];
        else               blank_cell(cell);
    } else {
        VTermPos pos = { row - scroll_off, col };
        vterm_screen_get_cell(vts, pos, cell);
    }
}

/* Fetch a cell by absolute line number; returns 0 if that line is gone. */
static int get_abs_cell(long abs, int col, VTermScreenCell *cell)
{
    long v = abs - (sb_total - sb_count);        /* index in history+screen */
    if (v < 0) return 0;
    if (v < sb_count) {
        SbLine *l = sb_get((int)v);
        if (col < l->cols) *cell = l->cells[col];
        else               blank_cell(cell);
        return 1;
    }
    long r = v - sb_count;
    if (r >= rows || col >= cols) return 0;
    VTermPos pos = { (int)r, col };
    vterm_screen_get_cell(vts, pos, cell);
    return 1;
}

static int pos_cmp(SelPos a, SelPos b)
{
    if (a.line != b.line) return a.line < b.line ? -1 : 1;
    if (a.col  != b.col)  return a.col  < b.col  ? -1 : 1;
    return 0;
}

static int in_selection(long line, int col)
{
    if (!sel_active) return 0;
    if (line < sel_s.line || line > sel_e.line) return 0;
    if (line == sel_s.line && col < sel_s.col) return 0;
    if (line == sel_e.line && col > sel_e.col) return 0;
    return 1;
}

static void draw_cell(int row, int col)
{
    VTermScreenCell cell;
    get_view_cell(row, col, &cell);

    if (cell.chars[0] == (uint32_t)-1)       /* right half of a wide char */
        return;

    int width = cell.width > 0 ? cell.width : 1;
    int x = col * cell_w, y = row * cell_h;
    int w = cell_w * width, h = cell_h;

    const VTermColor *fgc = &cell.fg, *bgc = &cell.bg;

    int invert = cell.attrs.reverse;
    if (scroll_off == 0 && cursor_visible &&
        row == cursor_pos.row && col == cursor_pos.col)
        invert = !invert;
    if (in_selection(sb_total - scroll_off + row, col))
        invert = !invert;
    if (invert) { const VTermColor *t = fgc; fgc = bgc; bgc = t; }

    frect(x, y, w, h, color_for(bgc)->pixel);

    uint32_t cp = cell.chars[0];
    if (cp && cp != ' ' && !cell.attrs.conceal &&
        !draw_special(cp, x, y, w, h, fgc, bgc)) {
        uint32_t chs[VTERM_MAX_CHARS_PER_CELL];
        int n = 0;
        while (n < VTERM_MAX_CHARS_PER_CELL && cell.chars[n]) {
            chs[n] = cell.chars[n];
            n++;
        }
        int synth, fb;
        XftFont *f = font_for(cp, cell.attrs.bold, cell.attrs.italic,
                              &synth, &fb);
        XftColor *xc = color_for(fgc);

        if (fb) {            /* keep oversized fallback glyphs inside the cell */
            XRectangle r = { 0, 0, (unsigned short)w, (unsigned short)h };
            XftDrawSetClipRectangles(xftdraw, x, y, &r, 1);
        }
        XftDrawString32(xftdraw, xc, f, x, y + cell_ascent,
                        (const FcChar32 *)chs, n);
        if (synth)
            XftDrawString32(xftdraw, xc, f, x + 1, y + cell_ascent,
                            (const FcChar32 *)chs, n);
        if (fb) XftDrawSetClip(xftdraw, NULL);
    }

    unsigned long fgpx = color_for(fgc)->pixel;
    XSetForeground(dpy, gc, fgpx);
    if (cell.attrs.underline) {
        XDrawLine(dpy, win, gc, x, y + h - 1, x + w - 1, y + h - 1);
        if (cell.attrs.underline == VTERM_UNDERLINE_DOUBLE)
            XDrawLine(dpy, win, gc, x, y + h - 3, x + w - 1, y + h - 3);
    }
    if (cell.attrs.strike)
        XDrawLine(dpy, win, gc, x, y + h / 2, x + w - 1, y + h / 2);
}

static void redraw_all(void)
{
    if (!win) return;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            draw_cell(r, c);
    XFlush(dpy);
}

static void redraw_abs_range(long a0, long a1)
{
    long base = sb_total - scroll_off;
    long r0 = a0 - base, r1 = a1 - base;
    if (r1 < 0 || r0 >= rows) return;
    if (r0 < 0) r0 = 0;
    if (r1 >= rows) r1 = rows - 1;
    for (long r = r0; r <= r1; r++)
        for (int c = 0; c < cols; c++)
            draw_cell((int)r, c);
    XFlush(dpy);
}

/* ---------- scrolling ---------- */
static void update_scrollbar(void)
{
    if (!vsb) return;
    XtVaSetValues(vsb,
                  XmNminimum,       0,
                  XmNmaximum,       sb_count + rows,
                  XmNsliderSize,    rows,
                  XmNpageIncrement, rows > 1 ? rows - 1 : 1,
                  XmNvalue,         sb_count - scroll_off,
                  NULL);
}

static void scroll_by(int lines)           /* positive = back in time */
{
    int n = scroll_off + lines;
    if (n < 0) n = 0;
    if (n > sb_count) n = sb_count;
    if (n == scroll_off) return;
    scroll_off = n;
    update_scrollbar();
    redraw_all();
}

static void scroll_to_bottom(void)
{
    if (scroll_off == 0) return;
    scroll_off = 0;
    update_scrollbar();
    redraw_all();
}

static void scrollbar_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)client;
    XmScrollBarCallbackStruct *cbs = (XmScrollBarCallbackStruct *)call;
    int n = sb_count - cbs->value;
    if (n < 0) n = 0;
    if (n > sb_count) n = sb_count;
    if (n == scroll_off) return;
    scroll_off = n;
    redraw_all();
}

static void cb_output_fwd(const char *s, size_t len);

/* ---------- selection ---------- */
static void sel_apply(int active, SelPos s, SelPos e)
{
    int oa = sel_active;
    SelPos os = sel_s, oe = sel_e;
    if (oa == active && (!active || (!pos_cmp(os, s) && !pos_cmp(oe, e))))
        return;

    sel_active = active;
    sel_s = s; sel_e = e;

    long a0 = LONG_MAX, a1 = LONG_MIN;
    if (oa)     { a0 = os.line; a1 = oe.line; }
    if (active) {
        if (s.line < a0) a0 = s.line;
        if (e.line > a1) a1 = e.line;
    }
    if (a0 <= a1) redraw_abs_range(a0, a1);
}

static void sel_clear(void) { sel_apply(0, sel_s, sel_e); }

static SelPos pos_from_xy(int x, int y)
{
    int c = x < 0 ? 0 : x / cell_w;
    int r = y < 0 ? 0 : y / cell_h;
    if (c >= cols) c = cols - 1;
    if (r >= rows) r = rows - 1;
    SelPos p = { sb_total - scroll_off + r, c };
    return p;
}

/* 0 = blank, 1 = word character, 2 = other punctuation */
static int char_class(SelPos p)
{
    VTermScreenCell c;
    if (!get_abs_cell(p.line, p.col, &c)) return 0;
    uint32_t ch = c.chars[0];
    if (ch == 0 || ch == ' ' || ch == (uint32_t)-1) return 0;
    if (ch >= 128) return 1;
    if (isalnum((int)ch) || strchr(WORD_CHARS, (int)ch)) return 1;
    return 2;
}

static SelPos snap_lo(SelPos p)
{
    if (sel_mode == 1) {
        int k = char_class(p);
        while (p.col > 0) {
            SelPos q = { p.line, p.col - 1 };
            if (char_class(q) != k) break;
            p = q;
        }
    } else if (sel_mode == 2) p.col = 0;
    return p;
}

static SelPos snap_hi(SelPos p)
{
    if (sel_mode == 1) {
        int k = char_class(p);
        while (p.col < cols - 1) {
            SelPos q = { p.line, p.col + 1 };
            if (char_class(q) != k) break;
            p = q;
        }
    } else if (sel_mode == 2) p.col = cols - 1;
    return p;
}

/* Recompute the selection for pointer position p while dragging. */
static void sel_update(SelPos p)
{
    SelPos s, e;
    if (pos_cmp(p, anchor_lo) < 0) { s = snap_lo(p); e = anchor_hi; }
    else                           { s = anchor_lo;  e = snap_hi(p); }
    sel_apply(sel_mode != 0 || pos_cmp(s, e) != 0, s, e);
}

/* ---------- text extraction / UTF-8 helpers ---------- */
typedef struct { char *p; size_t n, cap; } Buf;

static void buf_putc(Buf *b, char c)
{
    if (b->n + 1 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->p = realloc(b->p, b->cap);
        if (!b->p) abort();
    }
    b->p[b->n++] = c;
}

static void put_utf8(Buf *b, uint32_t cp)
{
    if (cp < 0x80)         buf_putc(b, (char)cp);
    else if (cp < 0x800)  { buf_putc(b, (char)(0xC0 | (cp >> 6)));
                            buf_putc(b, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000){ buf_putc(b, (char)(0xE0 | (cp >> 12)));
                            buf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
                            buf_putc(b, (char)(0x80 | (cp & 0x3F))); }
    else                  { buf_putc(b, (char)(0xF0 | (cp >> 18)));
                            buf_putc(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
                            buf_putc(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
                            buf_putc(b, (char)(0x80 | (cp & 0x3F))); }
}

/* UTF-8 -> Latin-1 (for the STRING target); result is XtMalloc'd */
static char *utf8_to_latin1(const char *s, unsigned long *outlen)
{
    char *o = XtMalloc((Cardinal)strlen(s) + 1);
    unsigned long n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        uint32_t cp; int len, i;
        if (*p < 0x80)              { cp = *p;        len = 1; }
        else if ((*p & 0xE0) == 0xC0){ cp = *p & 0x1F; len = 2; }
        else if ((*p & 0xF0) == 0xE0){ cp = *p & 0x0F; len = 3; }
        else if ((*p & 0xF8) == 0xF0){ cp = *p & 0x07; len = 4; }
        else                        { cp = '?';       len = 1; }
        for (i = 1; i < len && (p[i] & 0xC0) == 0x80; i++)
            cp = (cp << 6) | (p[i] & 0x3F);
        p += i;
        o[n++] = cp < 256 ? (char)cp : '?';
    }
    o[n] = 0;
    *outlen = n;
    return o;
}

/* Text of the current selection as UTF-8 (malloc'd), or NULL. */
static char *selection_text(void)
{
    if (!sel_active) return NULL;
    Buf b = { NULL, 0, 0 };

    for (long ln = sel_s.line; ln <= sel_e.line; ln++) {
        int c0 = (ln == sel_s.line) ? sel_s.col : 0;
        int c1 = (ln == sel_e.line) ? sel_e.col : cols - 1;
        size_t keep = b.n;               /* end of last non-blank char */

        for (int c = c0; c <= c1; c++) {
            VTermScreenCell cell;
            if (!get_abs_cell(ln, c, &cell)) continue;
            if (cell.chars[0] == (uint32_t)-1) continue;  /* wide-char tail */
            if (cell.chars[0] == 0 || cell.chars[0] == ' ') {
                buf_putc(&b, ' ');
                continue;
            }
            for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++)
                put_utf8(&b, cell.chars[i]);
            keep = b.n;
        }
        b.n = keep;                      /* trim trailing blanks */
        if (ln < sel_e.line) buf_putc(&b, '\n');
    }
    buf_putc(&b, 0);
    return b.p;
}

/* ---------- X selections: ownership ---------- */
static Boolean convert_cb(Widget w, Atom *sel, Atom *target, Atom *type_ret,
                          XtPointer *value_ret, unsigned long *len_ret,
                          int *fmt_ret)
{
    (void)w;
    const char *text = (*sel == XA_PRIMARY) ? primary_text : clip_text;
    if (!text) return False;

    if (*target == a_targets) {
        Atom *t = (Atom *)XtMalloc(sizeof(Atom) * 4);
        t[0] = a_targets; t[1] = a_utf8; t[2] = XA_STRING; t[3] = a_text;
        *type_ret = XA_ATOM; *value_ret = (XtPointer)t;
        *len_ret = 4; *fmt_ret = 32;
        return True;
    }
    if (*target == a_utf8) {
        size_t n = strlen(text);
        char *v = XtMalloc((Cardinal)n + 1);
        memcpy(v, text, n + 1);
        *type_ret = a_utf8; *value_ret = (XtPointer)v;
        *len_ret = n; *fmt_ret = 8;
        return True;
    }
    if (*target == XA_STRING || *target == a_text) {
        unsigned long n;
        char *v = utf8_to_latin1(text, &n);
        *type_ret = XA_STRING; *value_ret = (XtPointer)v;
        *len_ret = n; *fmt_ret = 8;
        return True;
    }
    return False;
}

static void lose_primary_cb(Widget w, Atom *sel)
{
    (void)w; (void)sel;
    if (reown_guard) return;
    free(primary_text);
    primary_text = NULL;
    sel_clear();                 /* highlight goes away, like xterm */
    XFlush(dpy);
}

static void lose_clip_cb(Widget w, Atom *sel)
{
    (void)w; (void)sel;
    if (reown_guard) return;
    free(clip_text);
    clip_text = NULL;
}

static void own_primary(void)
{
    char *t = selection_text();
    if (!t || !*t) { free(t); return; }
    free(primary_text);
    primary_text = t;
    reown_guard = 1;
    XtOwnSelection(draw, XA_PRIMARY, XtLastTimestampProcessed(dpy),
                   convert_cb, lose_primary_cb, NULL);
    reown_guard = 0;
}

static void copy_to_clipboard(void)
{
    char *t = selection_text();
    if (!t || !*t) { free(t); XBell(dpy, 0); return; }
    free(clip_text);
    clip_text = t;
    reown_guard = 1;
    XtOwnSelection(draw, a_clipboard, XtLastTimestampProcessed(dpy),
                   convert_cb, lose_clip_cb, NULL);
    reown_guard = 0;
}

static void select_all(void)
{
    SelPos s = { sb_total - sb_count, 0 };
    SelPos e = { sb_total + rows - 1, cols - 1 };
    sel_mode = 0;
    sel_apply(1, s, e);
    own_primary();
}

/* ---------- X selections: paste ---------- */
static void send_paste(const char *data, unsigned long len, int latin1)
{
    char *out = malloc(len * 2 + 1);
    if (!out) return;
    size_t n = 0;
    for (unsigned long i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c == 0x1b) continue;     /* no escape sequences via paste */
        if (c == '\r' && i + 1 < len && data[i + 1] == '\n') continue;
        if (c == '\n') c = '\r';     /* terminals expect CR for Enter */
        if (latin1 && c >= 0x80) {
            out[n++] = (char)(0xC0 | (c >> 6));
            out[n++] = (char)(0x80 | (c & 0x3F));
        } else
            out[n++] = (char)c;
    }
    scroll_to_bottom();
    vterm_keyboard_start_paste(vt);  /* ESC[200~ if the app enabled it */
    cb_output_fwd(out, n);
    vterm_keyboard_end_paste(vt);
    free(out);
}

static void paste_cb(Widget w, XtPointer client, Atom *selection, Atom *type,
                     XtPointer value, unsigned long *length, int *format)
{
    int tried_utf8 = client != NULL;
    if (!value || *type == XT_CONVERT_FAIL || *format != 8) {
        if (value) XtFree((char *)value);
        if (tried_utf8)              /* owner may only speak STRING */
            XtGetSelectionValue(w, *selection, XA_STRING, paste_cb, NULL,
                                XtLastTimestampProcessed(dpy));
        else
            XBell(dpy, 0);
        return;
    }
    send_paste((const char *)value, *length, *type == XA_STRING);
    XtFree((char *)value);
}

static void request_paste(Atom which)
{
    XtGetSelectionValue(draw, which, a_utf8, paste_cb, (XtPointer)1,
                        XtLastTimestampProcessed(dpy));
}

/* ---------- mouse ---------- */
static void autoscroll_cb(XtPointer client, XtIntervalId *id)
{
    (void)client; (void)id;
    autoscroll_id = 0;
    if (!sel_dragging) return;

    int outside = 0;
    if (ptr_y < 0)                 { scroll_by(1);  outside = 1; }
    else if (ptr_y >= rows * cell_h) { scroll_by(-1); outside = 1; }

    sel_update(pos_from_xy(ptr_x, ptr_y));
    if (outside)
        autoscroll_id = XtAppAddTimeOut(app, 50, autoscroll_cb, NULL);
}

static void mouse_cb(Widget w, XtPointer client, XEvent *ev, Boolean *cont)
{
    (void)client; (void)cont;

    switch (ev->type) {
    case ButtonPress: {
        XButtonEvent *b = &ev->xbutton;
        if (b->button == Button4) { scroll_by(WHEEL_LINES);  return; }
        if (b->button == Button5) { scroll_by(-WHEEL_LINES); return; }

        SelPos p = pos_from_xy(b->x, b->y);
        ptr_x = b->x; ptr_y = b->y;

        if (b->button == Button1) {
            XmProcessTraversal(w, XmTRAVERSE_CURRENT);   /* take focus */
            if (click_count > 0 &&
                b->time - last_click_time <= (Time)XtGetMultiClickTime(dpy) &&
                pos_cmp(p, last_click_pos) == 0)
                click_count = click_count % 3 + 1;   /* 1 -> 2 -> 3 -> 1 */
            else
                click_count = 1;
            last_click_time = b->time;
            last_click_pos  = p;

            sel_mode  = click_count - 1;             /* char / word / line */
            anchor_lo = snap_lo(p);
            anchor_hi = snap_hi(p);
            sel_dragging = 1;
            sel_update(p);
        } else if (b->button == Button2) {
            request_paste(XA_PRIMARY);
        } else if (b->button == Button3 && sel_active) {
            /* extend: keep the end farthest from the click fixed */
            if (pos_cmp(p, sel_s) < 0) anchor_lo = anchor_hi = sel_e;
            else                       anchor_lo = anchor_hi = sel_s;
            sel_mode = 0;
            sel_dragging = 1;
            sel_update(p);
        }
        break;
    }
    case MotionNotify: {
        if (!sel_dragging) return;
        ptr_x = ev->xmotion.x; ptr_y = ev->xmotion.y;
        sel_update(pos_from_xy(ptr_x, ptr_y));
        if ((ptr_y < 0 || ptr_y >= rows * cell_h) && !autoscroll_id)
            autoscroll_id = XtAppAddTimeOut(app, 50, autoscroll_cb, NULL);
        break;
    }
    case ButtonRelease: {
        unsigned bt = ev->xbutton.button;
        if ((bt == Button1 || bt == Button3) && sel_dragging) {
            sel_dragging = 0;
            if (autoscroll_id) {
                XtRemoveTimeOut(autoscroll_id);
                autoscroll_id = 0;
            }
            if (sel_active) own_primary();
        }
        break;
    }
    }
}

/* ---------- libvterm callbacks ---------- */
static int cb_damage(VTermRect rect, void *user)
{
    (void)user;
    if (!win) return 1;
    if (scroll_off > 0) { view_dirty = 1; return 1; }
    for (int r = rect.start_row; r < rect.end_row; r++)
        for (int c = rect.start_col; c < rect.end_col; c++)
            draw_cell(r, c);
    return 1;
}

static int cb_movecursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    (void)user;
    cursor_pos = pos;
    cursor_visible = visible;
    if (win && scroll_off == 0) {
        if (oldpos.row < rows && oldpos.col < cols)
            draw_cell(oldpos.row, oldpos.col);
        draw_cell(pos.row, pos.col);
    }
    return 1;
}

static int cb_bell(void *user)
{
    (void)user;
    XBell(dpy, 0);
    return 1;
}

static const VTermScreenCallbacks screen_cbs = {
    .damage       = cb_damage,
    .movecursor   = cb_movecursor,
    .bell         = cb_bell,
    .sb_pushline  = cb_sb_pushline,
    .sb_popline   = cb_sb_popline,
};

/* libvterm wants to send bytes to the application (keys, replies) */
static void cb_output(const char *s, size_t len, void *user)
{
    (void)user;
    while (len > 0) {
        ssize_t n = write(master_fd, s, len);
        if (n < 0) { if (errno == EINTR) continue; break; }
        s += n; len -= (size_t)n;
    }
}

static void cb_output_fwd(const char *s, size_t len) { cb_output(s, len, NULL); }

/* ---------- pty input ---------- */
static void pty_input_cb(XtPointer client, int *fd, XtInputId *id)
{
    (void)client; (void)id;
    char buf[8192];
    ssize_t n = read(*fd, buf, sizeof buf);
    if (n <= 0) {                    /* shell exited */
        vterm_free(vt);
        exit(0);
    }
    vterm_input_write(vt, buf, (size_t)n);
    vterm_screen_flush_damage(vts);

    if (sb_dirty)   { sb_dirty = 0;   update_scrollbar(); }
    if (view_dirty) { view_dirty = 0; redraw_all(); }
    XFlush(dpy);
}

/* ---------- keyboard ---------- */
static VTermKey map_special(KeySym ks)
{
    switch (ks) {
    case XK_Return: case XK_KP_Enter: return VTERM_KEY_ENTER;
    case XK_Tab: case XK_ISO_Left_Tab: return VTERM_KEY_TAB;
    case XK_BackSpace:  return VTERM_KEY_BACKSPACE;
    case XK_Escape:     return VTERM_KEY_ESCAPE;
    case XK_Up:         return VTERM_KEY_UP;
    case XK_Down:       return VTERM_KEY_DOWN;
    case XK_Left:       return VTERM_KEY_LEFT;
    case XK_Right:      return VTERM_KEY_RIGHT;
    case XK_Insert:     return VTERM_KEY_INS;
    case XK_Delete:     return VTERM_KEY_DEL;
    case XK_Home:       return VTERM_KEY_HOME;
    case XK_End:        return VTERM_KEY_END;
    case XK_Prior:      return VTERM_KEY_PAGEUP;
    case XK_Next:       return VTERM_KEY_PAGEDOWN;
    default:
        if (ks >= XK_F1 && ks <= XK_F12)
            return VTERM_KEY_FUNCTION(ks - XK_F1 + 1);
        return VTERM_KEY_NONE;
    }
}

static void input_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)client;
    XmDrawingAreaCallbackStruct *cbs = (XmDrawingAreaCallbackStruct *)call;
    XEvent *ev = cbs->event;
    if (!ev || ev->type != KeyPress) return;

    char buf[32];
    KeySym ks;
    int len = XLookupString(&ev->xkey, buf, sizeof buf, &ks, NULL);

    VTermModifier mod = VTERM_MOD_NONE;
    if (ev->xkey.state & ShiftMask)   mod |= VTERM_MOD_SHIFT;
    if (ev->xkey.state & ControlMask) mod |= VTERM_MOD_CTRL;
    if (ev->xkey.state & Mod1Mask)    mod |= VTERM_MOD_ALT;

    /* Shift+PageUp/PageDown scroll the history instead of going to the app */
    if ((mod & VTERM_MOD_SHIFT) && (ks == XK_Prior || ks == XK_Next)) {
        scroll_by(ks == XK_Prior ? rows - 1 : -(rows - 1));
        return;
    }

    /* Font size: Ctrl+Shift+Plus / Minus / 0 */
    if ((mod & VTERM_MOD_CTRL) && (mod & VTERM_MOD_SHIFT)) {
        if (ks == XK_plus || ks == XK_equal || ks == XK_KP_Add)
            { change_size(1, 0); return; }
        if (ks == XK_underscore || ks == XK_minus || ks == XK_KP_Subtract)
            { change_size(-1, 0); return; }
        if (ks == XK_parenright || ks == XK_0)
            { change_size(0, 1); return; }
    }

    /* Clipboard shortcuts: Ctrl+Shift+C/V, Ctrl+Insert, Shift+Insert */
    if ((mod & VTERM_MOD_CTRL) && (mod & VTERM_MOD_SHIFT) &&
        (ks == XK_C || ks == XK_c)) { copy_to_clipboard(); return; }
    if ((mod & VTERM_MOD_CTRL) && (mod & VTERM_MOD_SHIFT) &&
        (ks == XK_V || ks == XK_v)) { request_paste(a_clipboard); return; }
    if ((mod & VTERM_MOD_CTRL) && ks == XK_Insert) {
        copy_to_clipboard(); return;
    }
    if ((mod & VTERM_MOD_SHIFT) && ks == XK_Insert) {
        request_paste(XA_PRIMARY); return;
    }

    VTermKey vk = map_special(ks);
    if (vk != VTERM_KEY_NONE) {
        scroll_to_bottom();
        vterm_keyboard_key(vt, vk, mod);     /* handles app-cursor mode etc. */
        return;
    }
    if (len > 0) {
        scroll_to_bottom();
        if (mod & VTERM_MOD_ALT) cb_output("\033", 1, NULL);  /* Meta = ESC */
        cb_output(buf, (size_t)len, NULL);
    }
}

/* ---------- expose / resize ---------- */
static void expose_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)client; (void)call;
    redraw_all();
}

/* Recompute rows/cols from the drawing area's pixel size (after a window
 * resize or a font change) and tell libvterm and the pty. */
static void grid_resize(void)
{
    Dimension pw, ph;
    XtVaGetValues(draw, XmNwidth, &pw, XmNheight, &ph, NULL);

    int ncols = pw / cell_w, nrows = ph / cell_h;
    if (ncols < 1) ncols = 1;
    if (nrows < 1) nrows = 1;

    if (ncols != cols || nrows != rows) {
        cols = ncols; rows = nrows;
        vterm_set_size(vt, rows, cols);

        struct winsize ws = { .ws_row = rows, .ws_col = cols };
        if (master_fd >= 0) ioctl(master_fd, TIOCSWINSZ, &ws);
    }

    if (scroll_off > sb_count) scroll_off = sb_count;
    sb_dirty = view_dirty = 0;
    update_scrollbar();
    if (win) XClearWindow(dpy, win);
    redraw_all();
}

static void resize_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)client; (void)call;
    grid_resize();
}

/* Snap window resizing to whole character cells.  The base size is
 * everything that is not the character grid (menu bar, frame, scrollbar). */
static void update_wm_hints(void)
{
    Dimension tw, th, dw, dh;
    XtVaGetValues(toplevel, XmNwidth, &tw, XmNheight, &th, NULL);
    XtVaGetValues(draw,     XmNwidth, &dw, XmNheight, &dh, NULL);
    int bw = (int)tw - (int)dw, bh = (int)th - (int)dh;
    XtVaSetValues(toplevel,
                  XmNwidthInc,  cell_w, XmNheightInc, cell_h,
                  XmNbaseWidth, bw,     XmNbaseHeight, bh,
                  XmNminWidth,  bw + cell_w * 10,
                  XmNminHeight, bh + cell_h * 3,
                  NULL);
}

/* ---------- Xft fonts: loading, switching, chooser ---------- */
static XftFont *open_variant(FcPattern *base, int weight, int slant)
{
    FcPattern *p = FcPatternDuplicate(base);
    if (weight >= 0) {
        FcPatternDel(p, FC_WEIGHT);
        FcPatternAddInteger(p, FC_WEIGHT, weight);
    }
    if (slant >= 0) {
        FcPatternDel(p, FC_SLANT);
        FcPatternAddInteger(p, FC_SLANT, slant);
    }
    FcResult res;
    FcPattern *m = XftFontMatch(dpy, scr_num, p, &res);
    FcPatternDestroy(p);
    if (!m) return NULL;
    XftFont *f = XftFontOpenPattern(dpy, m);     /* owns m on success */
    if (!f) FcPatternDestroy(m);
    return f;
}

static void free_fonts(void)
{
    for (int i = 0; i < 4; i++)
        if (fonts[i]) { XftFontClose(dpy, fonts[i]); fonts[i] = NULL; }
    for (int i = 0; i < nfallback; i++)
        XftFontClose(dpy, fallback[i]);
    nfallback = 0;
    memset(missing, 0, sizeof missing);
    if (base_pat) { FcPatternDestroy(base_pat); base_pat = NULL; }
}

/* Load a font from a fontconfig pattern ("DejaVu Sans Mono:size=12").
 * Keeps the current font and returns 0 on failure. */
static int load_fonts(const char *name)
{
    FcPattern *pat = FcNameParse((const FcChar8 *)name);
    if (!pat) return 0;

    XftFont *reg = open_variant(pat, -1, -1);
    if (!reg) { FcPatternDestroy(pat); return 0; }

    XGlyphInfo ext;
    XftTextExtents8(dpy, reg, (const FcChar8 *)"M", 1, &ext);
    int cw = ext.xOff, ch = reg->ascent + reg->descent;
    if (cw < 1 || ch < 1) {
        XftFontClose(dpy, reg);
        FcPatternDestroy(pat);
        return 0;
    }

    free_fonts();
    base_pat = pat;
    fonts[0] = reg;
    fonts[1] = open_variant(pat, FC_WEIGHT_BOLD, -1);
    fonts[2] = open_variant(pat, -1, FC_SLANT_ITALIC);
    fonts[3] = open_variant(pat, FC_WEIGHT_BOLD, FC_SLANT_ITALIC);

    /* If fontconfig only gave us the regular face back, drop the "variant"
     * so bold is synthesized and italics just fall back to upright. */
    int v;
    if (fonts[1] && (FcPatternGetInteger(fonts[1]->pattern, FC_WEIGHT, 0, &v)
                     != FcResultMatch || v < FC_WEIGHT_DEMIBOLD)) {
        XftFontClose(dpy, fonts[1]); fonts[1] = NULL;
    }
    if (fonts[2] && (FcPatternGetInteger(fonts[2]->pattern, FC_SLANT, 0, &v)
                     != FcResultMatch || v == FC_SLANT_ROMAN)) {
        XftFontClose(dpy, fonts[2]); fonts[2] = NULL;
    }
    if (fonts[3] && (FcPatternGetInteger(fonts[3]->pattern, FC_SLANT, 0, &v)
                     != FcResultMatch || v == FC_SLANT_ROMAN)) {
        XftFontClose(dpy, fonts[3]); fonts[3] = NULL;
    }

    cell_w = cw;
    cell_h = ch;
    cell_ascent = reg->ascent;
    free(font_name);
    font_name = strdup(name);
    return 1;
}

static double current_size(void)
{
    double d;
    if (fonts[0] &&
        FcPatternGetDouble(fonts[0]->pattern, FC_SIZE, 0, &d) == FcResultMatch)
        return d;
    return 11.0;
}

/* malloc'd copy of 'name' with its size replaced (or added) */
static char *name_with_size(const char *name, double size)
{
    FcPattern *p = FcNameParse((const FcChar8 *)name);
    if (!p) return NULL;
    FcPatternDel(p, FC_PIXEL_SIZE);
    FcPatternDel(p, FC_SIZE);
    FcPatternAddDouble(p, FC_SIZE, size);
    FcChar8 *s = FcNameUnparse(p);
    FcPatternDestroy(p);
    if (!s) return NULL;
    char *out = strdup((const char *)s);
    FcStrFree(s);
    return out;
}

static int apply_font(const char *name)
{
    if (!load_fonts(name)) { XBell(dpy, 0); return 0; }
    update_wm_hints();
    grid_resize();                   /* same window pixels, new cell grid */
    return 1;
}

static void change_size(double delta, int reset)
{
    double sz = reset ? default_size : current_size() + delta;
    if (sz < 4)  sz = 4;
    if (sz > 72) sz = 72;
    char *n = name_with_size(font_name, sz);
    if (n) { apply_font(n); free(n); }
}

/* text typed in the font dialog -> pattern; adds the current size if none */
static void apply_font_text(const char *txt)
{
    while (*txt == ' ') txt++;
    if (!*txt) return;
    FcPattern *p = FcNameParse((const FcChar8 *)txt);
    if (!p) { XBell(dpy, 0); return; }
    double d;
    if (FcPatternGetDouble(p, FC_SIZE, 0, &d) != FcResultMatch &&
        FcPatternGetDouble(p, FC_PIXEL_SIZE, 0, &d) != FcResultMatch)
        FcPatternAddDouble(p, FC_SIZE, current_size());
    FcChar8 *s = FcNameUnparse(p);
    FcPatternDestroy(p);
    if (!s) return;
    apply_font((const char *)s);
    FcStrFree(s);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* installed monospace families, as XmStrings for the chooser list */
static XmString *list_mono_families(int *count)
{
    FcPattern *pat = FcPatternCreate();
    FcPatternAddInteger(pat, FC_SPACING, FC_MONO);
    FcObjectSet *os = FcObjectSetCreate();
    FcObjectSetAdd(os, FC_FAMILY);
    FcFontSet *fs = FcFontList(NULL, pat, os);

    int cap = (fs ? fs->nfont : 0) + 1, n = 0;
    char **names = calloc((size_t)cap, sizeof *names);
    names[n++] = strdup("monospace");
    for (int i = 0; fs && i < fs->nfont; i++) {
        FcChar8 *fam;
        if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, &fam) != FcResultMatch)
            continue;
        FcPattern *q = FcPatternCreate();      /* unparse => proper escaping */
        FcPatternAddString(q, FC_FAMILY, fam);
        FcChar8 *u = FcNameUnparse(q);
        FcPatternDestroy(q);
        if (u) { names[n++] = strdup((const char *)u); FcStrFree(u); }
    }
    qsort(names + 1, (size_t)(n - 1), sizeof *names, cmp_str);

    XmString *items = calloc((size_t)n, sizeof *items);
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (i > 1 && strcmp(names[i], names[i - 1]) == 0) continue;   /* dedupe */
        items[m++] = XmStringCreateLocalized(names[i]);
    }
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
    if (fs) FcFontSetDestroy(fs);
    FcObjectSetDestroy(os);
    FcPatternDestroy(pat);
    *count = m;
    return items;
}

static void font_apply_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)client;
    XmSelectionBoxCallbackStruct *cbs = (XmSelectionBoxCallbackStruct *)call;
    char *s = NULL;
    if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &s) && s) {
        apply_font_text(s);
        XtFree(s);
    }
}

static void font_dialog_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)client; (void)call;
    static Widget dlg;
    if (!dlg) {
        int n;
        XmString *items = list_mono_families(&n);
        XmString label = XmStringCreateLocalized(
            "Xft font (family, optionally with :size=N or :bold etc.):");
        XmString title = XmStringCreateLocalized("Choose Font");
        XmString apply = XmStringCreateLocalized("Apply");
        Arg args[8];
        int i = 0;
        XtSetArg(args[i], XmNlistItems, items);            i++;
        XtSetArg(args[i], XmNlistItemCount, n);            i++;
        XtSetArg(args[i], XmNlistVisibleItemCount, 12);    i++;
        XtSetArg(args[i], XmNmustMatch, False);            i++;
        XtSetArg(args[i], XmNselectionLabelString, label); i++;
        XtSetArg(args[i], XmNdialogTitle, title);          i++;
        XtSetArg(args[i], XmNapplyLabelString, apply);     i++;
        dlg = XmCreateSelectionDialog(toplevel, "fontdialog", args, i);
        XtUnmanageChild(XmSelectionBoxGetChild(dlg, XmDIALOG_HELP_BUTTON));
        XtAddCallback(dlg, XmNokCallback,    font_apply_cb, NULL);
        XtAddCallback(dlg, XmNapplyCallback, font_apply_cb, NULL);
        for (int k = 0; k < n; k++) XmStringFree(items[k]);
        free(items);
        XmStringFree(label); XmStringFree(title); XmStringFree(apply);
    }
    XmString cur = XmStringCreateLocalized(font_name);
    XtVaSetValues(dlg, XmNtextString, cur, NULL);
    XmStringFree(cur);
    XtManageChild(dlg);
}

static void larger_cb(Widget w, XtPointer c, XtPointer d)
{ (void)w; (void)c; (void)d; change_size(1, 0); }
static void smaller_cb(Widget w, XtPointer c, XtPointer d)
{ (void)w; (void)c; (void)d; change_size(-1, 0); }
static void default_size_cb(Widget w, XtPointer c, XtPointer d)
{ (void)w; (void)c; (void)d; change_size(0, 1); }

/* ---------- menu callbacks ---------- */
static void quit_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)client; (void)call;
    if (child_pid > 0) kill(child_pid, SIGHUP);
    exit(0);
}

static void copy_cb(Widget w, XtPointer client, XtPointer call)
{ (void)w; (void)client; (void)call; copy_to_clipboard(); }

static void paste_menu_cb(Widget w, XtPointer client, XtPointer call)
{ (void)w; (void)client; (void)call; request_paste(a_clipboard); }

static void select_all_cb(Widget w, XtPointer client, XtPointer call)
{ (void)w; (void)client; (void)call; select_all(); }

static void reset_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)client; (void)call;
    sel_clear();
    vterm_screen_reset(vts, 1);
    scroll_off = 0;
    update_scrollbar();
    redraw_all();
}

static void clear_sb_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)client; (void)call;
    sel_clear();
    clear_scrollback_lines();
    update_scrollbar();
    redraw_all();
}

static void about_cb(Widget w, XtPointer client, XtPointer call)
{
    (void)w; (void)client; (void)call;
    static Widget dialog;
    if (!dialog) {
        XmString msg = XmStringCreateLtoR(
            "mterm\n\nA tiny terminal emulator\nbuilt with OpenMotif and libvterm.",
            XmFONTLIST_DEFAULT_TAG);
        Arg args[2];
        XtSetArg(args[0], XmNmessageString, msg);
        XtSetArg(args[1], XmNdialogTitle,
                 XmStringCreateLocalized("About mterm"));
        dialog = XmCreateInformationDialog(toplevel, "about", args, 2);
        XtUnmanageChild(XmMessageBoxGetChild(dialog, XmDIALOG_CANCEL_BUTTON));
        XtUnmanageChild(XmMessageBoxGetChild(dialog, XmDIALOG_HELP_BUTTON));
        XmStringFree(msg);
    }
    XtManageChild(dialog);
}

/* ---------- menu construction helpers ---------- */
static Widget add_menu(Widget bar, const char *name, KeySym mnemonic,
                       Widget *cascade_out)
{
    Widget pane = XmCreatePulldownMenu(bar, (char *)name, NULL, 0);
    Widget casc = XtVaCreateManagedWidget(name, xmCascadeButtonWidgetClass,
                                          bar,
                                          XmNsubMenuId, pane,
                                          XmNmnemonic,  mnemonic,
                                          NULL);
    if (cascade_out) *cascade_out = casc;
    return pane;
}

static void add_item(Widget pane, const char *label, KeySym mnemonic,
                     const char *accel, XtCallbackProc cb)
{
    Widget b = XtVaCreateManagedWidget(label, xmPushButtonGadgetClass, pane,
                                       XmNmnemonic, mnemonic, NULL);
    if (accel) {            /* display only; keys are handled in input_cb */
        XmString s = XmStringCreateLocalized((char *)accel);
        XtVaSetValues(b, XmNacceleratorText, s, NULL);
        XmStringFree(s);
    }
    XtAddCallback(b, XmNactivateCallback, cb, NULL);
}

static void add_separator(Widget pane)
{
    XtVaCreateManagedWidget("sep", xmSeparatorGadgetClass, pane, NULL);
}

/* ---------- main ---------- */
int main(int argc, char **argv)
{
    /* our own options, removed from argv before the toolkit sees them */
    {
        int j = 1;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-fa") && i + 1 < argc) { opt_fa = argv[++i]; continue; }
            if (!strcmp(argv[i], "-fs") && i + 1 < argc) { opt_fs = atof(argv[++i]); continue; }
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                printf("usage: mterm [-fa FONT] [-fs SIZE]\n"
                       "  -fa  fontconfig pattern, e.g. \"DejaVu Sans Mono:size=12\"\n"
                       "  -fs  font size in points\n"
                       "  X resource: MTerm*faceName\n");
                return 0;
            }
            argv[j++] = argv[i];
        }
        argv[j] = NULL;
        argc = j;
    }

    toplevel = YkInitWindow(&app, "mterm", &argc, argv);
    XtVaSetValues(toplevel, XmNtitle, "Command Center", NULL);
    XtVaSetValues(toplevel, XmNwidth, 720, XmNheight, 480, NULL);
    dpy = XtDisplay(toplevel);
    a_utf8      = XInternAtom(dpy, "UTF8_STRING", False);
    a_targets   = XInternAtom(dpy, "TARGETS",     False);
    a_clipboard = XInternAtom(dpy, "CLIPBOARD",   False);
    a_text      = XInternAtom(dpy, "TEXT",        False);

    /* Xft font: -fa/-fs, else the faceName resource, else monospace:size=11 */
    scr_num = XScreenNumberOfScreen(XtScreen(toplevel));
    vis     = DefaultVisual(dpy, scr_num);
    cmap    = DefaultColormap(dpy, scr_num);
    {
        AppRes ar;
        XtGetApplicationResources(toplevel, &ar, app_res, 1, NULL, 0);
        const char *name = opt_fa ? opt_fa : ar.face;
        char *sized = opt_fs > 0 ? name_with_size(name, opt_fs) : NULL;
        if (!load_fonts(sized ? sized : name)) {
            fprintf(stderr, "mterm: cannot load font '%s', using default\n", name);
            if (!load_fonts("monospace:size=11")) {
                fprintf(stderr, "mterm: no usable font\n");
                return 1;
            }
        }
        free(sized);
        default_size = current_size();
    }

    /* libvterm setup */
    vt = vterm_new(rows, cols);
    vterm_set_utf8(vt, 1);
    vterm_output_set_callback(vt, cb_output, NULL);
    vts = vterm_obtain_screen(vt);
    vterm_screen_enable_altscreen(vts, 1);
    vterm_screen_set_callbacks(vts, &screen_cbs, NULL);
    vterm_screen_set_damage_merge(vts, VTERM_DAMAGE_ROW);

    vterm_color_rgb(&default_fg, 220, 220, 220);
    vterm_color_rgb(&default_bg, 20, 20, 24);
    vterm_screen_set_default_colors(vts, &default_fg, &default_bg);
    vterm_screen_reset(vts, 1);

    /* Main window: menu bar on top, form (terminal + scrollbar) below */
    Widget main_w  = XmCreateMainWindow(toplevel, "main", NULL, 0);
    XtManageChild(main_w);

    Widget menubar = XmCreateMenuBar(main_w, "menubar", NULL, 0);
    XtManageChild(menubar);

    Widget pane, casc;
    pane = add_menu(menubar, "File", 'F', NULL);
    add_item(pane, "Quit", 'Q', NULL, quit_cb);

    pane = add_menu(menubar, "Edit", 'E', NULL);
    add_item(pane, "Copy",       'C', "Ctrl+Shift+C", copy_cb);
    add_item(pane, "Paste",      'P', "Ctrl+Shift+V", paste_menu_cb);
    add_item(pane, "Select All", 'A', NULL,           select_all_cb);
    add_separator(pane);
    add_item(pane, "Reset Terminal",   'R', NULL, reset_cb);
    add_item(pane, "Clear Scrollback", 'l', NULL, clear_sb_cb);

    pane = add_menu(menubar, "Font", 'o', NULL);
    add_item(pane, "Choose Font...", 'C', NULL,            font_dialog_cb);
    add_separator(pane);
    add_item(pane, "Larger",         'L', "Ctrl+Shift++",  larger_cb);
    add_item(pane, "Smaller",        'S', "Ctrl+Shift+-",  smaller_cb);
    add_item(pane, "Default Size",   'D', "Ctrl+Shift+0",  default_size_cb);

    pane = add_menu(menubar, "Help", 'H', &casc);
    XtVaSetValues(menubar, XmNmenuHelpWidget, casc, NULL);
    add_item(pane, "About", 'A', NULL, about_cb);

    Widget form = XmCreateForm(main_w, "form", NULL, 0);

    vsb = XtVaCreateManagedWidget("scrollbar", xmScrollBarWidgetClass, form,
              XmNorientation,      XmVERTICAL,
              XmNtopAttachment,    XmATTACH_FORM,
              XmNbottomAttachment, XmATTACH_FORM,
              XmNrightAttachment,  XmATTACH_FORM,
              XmNminimum,          0,
              XmNmaximum,          rows,
              XmNsliderSize,       rows,
              XmNvalue,            0,
              XmNtraversalOn,      False,    /* keep keyboard focus on screen */
              NULL);
    XtAddCallback(vsb, XmNvalueChangedCallback, scrollbar_cb, NULL);
    XtAddCallback(vsb, XmNdragCallback,         scrollbar_cb, NULL);

    /* Sunken bevel around the terminal, classic Motif style */
    Widget frame = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, form,
              XmNshadowType,       XmSHADOW_IN,
              XmNshadowThickness,  2,
              XmNleftAttachment,   XmATTACH_FORM,
              XmNtopAttachment,    XmATTACH_FORM,
              XmNbottomAttachment, XmATTACH_FORM,
              XmNrightAttachment,  XmATTACH_WIDGET,
              XmNrightWidget,      vsb,
              NULL);

    draw = XtVaCreateManagedWidget("screen", xmDrawingAreaWidgetClass, frame,
              XmNwidth,            cols * cell_w,
              XmNheight,           rows * cell_h,
              XmNtraversalOn,      True,
              XmNresizePolicy,     XmRESIZE_ANY,
              NULL);
    XtAddCallback(draw, XmNexposeCallback, expose_cb, NULL);
    XtAddCallback(draw, XmNresizeCallback, resize_cb, NULL);
    XtAddCallback(draw, XmNinputCallback,  input_cb,  NULL);
    XtAddEventHandler(draw, ButtonPressMask | ButtonReleaseMask |
                            Button1MotionMask | Button3MotionMask,
                      False, mouse_cb, NULL);

    XtManageChild(form);
    XmMainWindowSetAreas(main_w, menubar, NULL, NULL, NULL, form);

    XtRealizeWidget(toplevel);
    win = XtWindow(draw);
    gc = XCreateGC(dpy, win, 0, NULL);
    xftdraw = XftDrawCreate(dpy, win, vis, cmap);
    XDefineCursor(dpy, win, XCreateFontCursor(dpy, XC_xterm));   /* I-beam */

    update_wm_hints();

    /* Start the shell on a pty */
    struct winsize ws = { .ws_row = rows, .ws_col = cols };
    child_pid = forkpty(&master_fd, NULL, NULL, &ws);
    if (child_pid < 0) { perror("forkpty"); return 1; }
    if (child_pid == 0) {
        const char *shell = getenv("SHELL");
        if (!shell || !*shell) shell = "/bin/sh";
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        execlp(shell, shell, (char *)NULL);
        perror("execlp");
        _exit(127);
    }

    XtAppAddInput(app, master_fd, (XtPointer)XtInputReadMask,
                  pty_input_cb, NULL);

    XmProcessTraversal(draw, XmTRAVERSE_CURRENT);   /* grab keyboard focus */
    XtAppMainLoop(app);
    return 0;
}
