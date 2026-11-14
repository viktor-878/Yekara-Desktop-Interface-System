/*
 * Desktop window: wallpaper, icons for the desktop folder, "My Computer".
 * See desktop.h for the overview.
 *
 * desktop.entry (YeKara entry format) layout:
 *
 *    [Desktop]                       <- optional, hand-edited, never rewritten
 *    Wallpaper=/path/to.xpm;
 *    Color=#008080;
 *
 *    [Icons]                         <- owned by this program
 *    Icon=<name>:<x>:<y>;      <- name is %XX-escaped for ':' ';' '\' '%'
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pwd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/xpm.h>

#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/Container.h>
#include <Xm/IconG.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/MwmUtil.h>

#include <yk/entry.h>
#include <yk/style.h>

#include "desktop.h"
#include "config.h"
#include "deskapp.h"
#include "dialogs.h"
#include "entries.h"
#include "fsops.h"
#include "icons.h"
#include "labels.h"
#include "launch.h"
#include "menus.h"
#include "state.h"
#include "util.h"

static Widget desktop_form   = NULL;
static Widget computer_popup = NULL;

/* =========================================================================
   PATHS
   ========================================================================= */

static const char *home_dir(void)
{
    const char *h = getenv("HOME");
    if (h && *h)
        return h;
    struct passwd *pw = getpwuid(getuid());
    return (pw && pw->pw_dir && *pw->pw_dir) ? pw->pw_dir : NULL;
}

static int mkdir_p(const char *path, mode_t mode)
{
    char buf[PATH_MAX];
    struct stat st;

    snprintf(buf, sizeof(buf), "%s", path);

    for (char *p = buf + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(buf, mode) != 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    if (mkdir(buf, mode) != 0 && errno != EEXIST)
        return -1;

    return (stat(buf, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1;
}

int desktop_resolve_dir(char *out, size_t n)
{
    char dir[PATH_MAX], resolved[PATH_MAX];
    const char *env = getenv(DESKTOP_DIR_ENV);

    if (env && *env) {
        snprintf(dir, sizeof(dir), "%s", env);
    } else {
        const char *home = home_dir();
        if (!home) {
            fprintf(stderr, "desktop: cannot determine the home directory "
                            "(set %s).\n", DESKTOP_DIR_ENV);
            return -1;
        }
        join_path(dir, sizeof(dir), home, DESKTOP_DEFAULT_SUBDIR);
    }

    if (mkdir_p(dir, 0755) != 0) {
        fprintf(stderr, "desktop: cannot use \"%s\" as the desktop folder: %s\n",
                dir, strerror(errno));
        return -1;
    }
    if (!realpath(dir, resolved)) {
        fprintf(stderr, "desktop: cannot resolve \"%s\": %s\n", dir, strerror(errno));
        return -1;
    }

    snprintf(out, n, "%s", resolved);
    return 0;
}

/* Returns the path to read preferences from:
 * 1. User's personal config file if it exists.
 * 2. Fallback to the bundle's default ("preferences/desktop.entry"). */
static Boolean prefs_read_file(char *out, size_t n)
{
    const char *home = home_dir();
    char dir[PATH_MAX];

    if (home) {
        join_path(dir, sizeof(dir), home, PREFS_SUBDIR);
        join_path(out, n, dir, DESKTOP_ENTRY_FILE);
        if (access(out, R_OK) == 0)
            return True;
    }

    /* Fallback to bundle default preferences */
    if (access("preferences/desktop.entry", R_OK) == 0) {
        snprintf(out, n, "%s", "preferences/desktop.entry");
        return True;
    }

    /* Final fallback to user path */
    if (home) {
        join_path(dir, sizeof(dir), home, PREFS_SUBDIR);
        join_path(out, n, dir, DESKTOP_ENTRY_FILE);
        return True;
    }

    return False;
}

/* Returns the path where user changes and icon positions are ALWAYS saved */
static Boolean prefs_file(char *out, size_t n)
{
    const char *home = home_dir();
    char dir[PATH_MAX];

    if (!home)
        return False;
    join_path(dir, sizeof(dir), home, PREFS_SUBDIR);
    join_path(out, n, dir, DESKTOP_ENTRY_FILE);
    return True;
}

/* =========================================================================
   ICON POSITION MEMORY  (desktop.entry persistence)
   ========================================================================= */

static IconPosition positions[MAX_ITEMS + 1];    /* current / loaded */
static int         npos = 0;
static IconPosition saved[MAX_ITEMS + 1];        /* what is on disk  */
static int         nsaved = 0;
static Boolean     positions_loaded = False;
static Boolean     icons_drawn = False;    /* false until first render */

static void store_position(IconPosition *tab, int *count, const char *name, int x, int y)
{
    for (int i = 0; i < *count; i++) {
        if (strcmp(tab[i].name, name) == 0) {
            tab[i].x = x;
            tab[i].y = y;
            return;
        }
    }
    if (*count < MAX_ITEMS + 1) {
        IconPosition *p = &tab[(*count)++];
        snprintf(p->name, sizeof(p->name), "%s", name);
        p->x = x;
        p->y = y;
    }
}

static Boolean find_position(const char *name, int *x, int *y)
{
    for (int i = 0; i < npos; i++) {
        if (strcmp(positions[i].name, name) == 0) {
            *x = positions[i].x;
            *y = positions[i].y;
            return True;
        }
    }
    return False;
}

static void encode_name(const char *s, char *out, size_t n)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t len = strlen(s), o = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        int esc = c < 0x20 || c == 0x7f || c == '%' || c == ':' || c == ';' ||
                  c == '\\' || (c == ' ' && (i == 0 || i == len - 1));

        if (o + (esc ? 3 : 1) + 1 > n)
            break;
        if (esc) {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = '\0';
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void decode_name(const char *s, char *out, size_t n)
{
    size_t o = 0;

    for (; *s && o + 1 < n; s++) {
        if (s[0] == '%' && hexval((unsigned char)s[1]) >= 0 &&
                          hexval((unsigned char)s[2]) >= 0) {
            out[o++] = (char)(hexval((unsigned char)s[1]) * 16 +
                              hexval((unsigned char)s[2]));
            s += 2;
        } else {
            out[o++] = *s;
        }
    }
    out[o] = '\0';
}

static void load_positions(void)
{
    char path[PATH_MAX * 2];

    positions_loaded = True;
    npos = nsaved = 0;

    if (!prefs_file(path, sizeof(path)))
        return;

    EntryDocument *doc = parse_entry_file(path);
    if (!doc)
        return;

    for (EntrySection *sec = doc->sections; sec; sec = sec->next) {
        if (strcmp(sec->name, "Icons") != 0)
            continue;

        for (EntryNode *e = sec->entries; e; e = e->next) {
            char name[256];

            if (strcmp(e->key, "Icon") != 0 || e->value_count < 3)
                continue;

            decode_name(e->values[0], name, sizeof(name));
            if (name[0] == '\0')
                continue;

            store_position(positions, &npos, name,
                           atoi(e->values[1]), atoi(e->values[2]));
        }
    }

    free_entry_document(doc);

    memcpy(saved, positions, sizeof(IconPosition) * (size_t)npos);
    nsaved = npos;
}

static char *section_header(char *line)
{
    while (isspace((unsigned char)*line))
        line++;
    if (*line != '[')
        return NULL;

    char *end = line + strlen(line);
    while (end > line && isspace((unsigned char)end[-1]))
        end--;
    if (end - line < 2 || end[-1] != ']')
        return NULL;

    end[-1] = '\0';
    line++;
    while (isspace((unsigned char)*line))
        line++;
    char *t = line + strlen(line);
    while (t > line && isspace((unsigned char)t[-1]))
        *--t = '\0';
    return line;
}

static void save_positions(void)
{
    char path[PATH_MAX * 2], dir[PATH_MAX * 2], tmp[PATH_MAX * 2 + 8];

    if (!prefs_file(path, sizeof(path)))
        return;

    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash)
        *slash = '\0';
    if (mkdir_p(dir, 0755) != 0)
        return;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *out = fopen(tmp, "w");
    if (!out)
        return;

    char  *keep = NULL;
    size_t keep_len = 0;
    FILE *in = fopen(path, "r");
    if (in) {
        char  *line = NULL;
        size_t cap = 0;
        int    in_icons = 0;

        while (getline(&line, &cap, in) != -1) {
            char *copy = strdup(line);
            if (!copy)
                break;
            char *hdr = section_header(copy);
            if (hdr)
                in_icons = (strcmp(hdr, "Icons") == 0);
            free(copy);

            if (in_icons)
                continue;

            size_t l = strlen(line);
            char *grown = realloc(keep, keep_len + l + 1);
            if (!grown)
                break;
            keep = grown;
            memcpy(keep + keep_len, line, l + 1);
            keep_len += l;
        }
        free(line);
        fclose(in);
    }

    while (keep_len > 0 && isspace((unsigned char)keep[keep_len - 1]))
        keep_len--;
    if (keep_len > 0) {
        fwrite(keep, 1, keep_len, out);
        fputs("\n\n", out);
    }
    free(keep);

    fputs("[Icons]\n", out);
    for (int i = 0; i < npos; i++) {
        char enc[800];
        encode_name(positions[i].name, enc, sizeof(enc));
        fprintf(out, "Icon=%s:%d:%d;\n", enc, positions[i].x, positions[i].y);
    }

    if (fclose(out) != 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return;
    }

    memcpy(saved, positions, sizeof(IconPosition) * (size_t)npos);
    nsaved = npos;
}

static Boolean positions_changed(void)
{
    if (npos != nsaved)
        return True;
    for (int i = 0; i < npos; i++) {
        int j;
        for (j = 0; j < nsaved; j++)
            if (strcmp(positions[i].name, saved[j].name) == 0)
                break;
        if (j == nsaved || positions[i].x != saved[j].x || positions[i].y != saved[j].y)
            return True;
    }
    return False;
}

static void remember_positions(void)
{
    WidgetList children = NULL;
    Cardinal   num = 0;

    if (!gridContainer || !icons_drawn)
        return;

    XtVaGetValues(gridContainer, XmNchildren, &children, XmNnumChildren, &num, NULL);
    if (num == 0)
        return;

    npos = 0;
    for (Cardinal i = 0; i < num; i++) {
        XtPointer nm = NULL;
        Position  x = 0, y = 0;

        XtVaGetValues(children[i], XmNuserData, &nm, XmNx, &x, XmNy, &y, NULL);
        if (nm)
            store_position(positions, &npos, (const char *)nm, (int)x, (int)y);
    }
}

static void persist_if_changed(void)
{
    if (positions_changed())
        save_positions();
}

/* =========================================================================
   LAYOUT
   ========================================================================= */

static void layout_icons(const char **names, int count, int *xs, int *ys)
{
    Screen *scr = XtScreen(toplevel_widget);
    int sw = WidthOfScreen(scr), sh = HeightOfScreen(scr);

    /* Reserve 200 pixels at the bottom for the toolbox/iconified apps */
    int reserved_bottom = 80;
    int usable_height = sh - reserved_bottom;

    int rows = (usable_height - DESKTOP_MARGIN) / DESKTOP_CELL_H;
    int cols = (sw - 2 * DESKTOP_MARGIN) / DESKTOP_CELL_W;
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;

    size_t nslots = (size_t)rows * (size_t)cols;
    unsigned char *occupied = calloc(nslots, 1);
    unsigned char *placed   = calloc((size_t)count, 1);
    if (!occupied || !placed) {
        free(occupied);
        free(placed);
        for (int i = 0; i < count; i++) {     /* degrade: plain column */
            xs[i] = sw - DESKTOP_MARGIN - DESKTOP_CELL_W;
            ys[i] = DESKTOP_MARGIN + i * DESKTOP_CELL_H;
        }
        return;
    }

    /* 1. Place remembered/saved icons first */
    for (int i = 0; i < count; i++) {
        int x, y;
        if (!find_position(names[i], &x, &y))
            continue;
        /* Allow off-screen or bottom-200px checks if placed manually by user */
        if (x < 0 || y < 0 || x > sw - 20 || y > sh - 20)
            continue;

        xs[i] = x;
        ys[i] = y;
        placed[i] = 1;

        /* Map saved position back to our right-to-left grid if within auto-layout bounds */
        int c = (sw - DESKTOP_MARGIN - x - DESKTOP_CELL_W / 2) / DESKTOP_CELL_W;
        int r = (y - DESKTOP_MARGIN + DESKTOP_CELL_H / 2) / DESKTOP_CELL_H;
        if (c >= 0 && c < cols && r >= 0 && r < rows)
            occupied[(size_t)c * (size_t)rows + (size_t)r] = 1;
    }

    /* 2. Auto-layout remaining unplaced icons from top-right, going left */
    size_t slot = 0;
    for (int i = 0; i < count; i++) {
        if (placed[i])
            continue;

        while (slot < nslots && occupied[slot])
            slot++;

        if (slot < nslots) {
            int c = (int)(slot / (size_t)rows); // column index from the right (0 = rightmost)
            int r = (int)(slot % (size_t)rows); // row index from the top

            /* Top-Right coordinate calculation */
            xs[i] = sw - DESKTOP_MARGIN - (c + 1) * DESKTOP_CELL_W;
            ys[i] = DESKTOP_MARGIN + r * DESKTOP_CELL_H;
            slot++;
        } else {
            /* Fallback if grid overflows */
            xs[i] = DESKTOP_MARGIN;
            ys[i] = DESKTOP_MARGIN;
        }
    }

    free(occupied);
    free(placed);
}

/* =========================================================================
   RENDER
   ========================================================================= */

static struct timespec last_mtime;

static void snapshot_dir_mtime(void)
{
    struct stat st;
    if (stat(current_path, &st) == 0)
        last_mtime = st.st_mtim;
}

void desktop_render(void)
{
    if (!positions_loaded)
        load_positions();
    remember_positions();

    selected_node_name[0] = '\0';
    XtVaSetValues(gridContainer, XmNselectedObjectCount, 0, NULL);
    destroy_all_children(gridContainer);

    free(g_items);
    g_items = NULL;
    g_item_count = scan_directory(current_path, &g_items);

    int count = g_item_count + 1;
    const char **names = malloc((size_t)count * sizeof(char *));
    int *xs = malloc((size_t)count * sizeof(int));
    int *ys = malloc((size_t)count * sizeof(int));

    if (names && xs && ys) {
        names[0] = DESKTOP_COMPUTER_ID;
        for (int i = 0; i < g_item_count; i++)
            names[i + 1] = g_items[i].name;

        layout_icons(names, count, xs, ys);

        Widget g = create_entry(gridContainer, DESKTOP_COMPUTER_ID, DESKTOP_COMPUTER_LABEL,
                     iconComputer, iconComputerMask, iconComputerValid,
                     XmLARGE_ICON, True, xs[0], ys[0], NULL, NULL,
                     ICON_LABEL_MAX_WIDTH, ICON_LABEL_MAX_LINES);
        XtVaSetValues(g, XmNshadowThickness, DESKTOP_ICON_SHADOW, NULL);

        for (int i = 0; i < g_item_count; i++) {
            Pixmap icon, mask;
            Boolean valid;
            unsigned int iw, ih;

            resolve_icon(i + 1, &icon, &mask, &valid, &iw, &ih);
            Widget ig = create_entry(gridContainer, g_items[i].name, g_items[i].name,
                          icon, mask, valid, XmLARGE_ICON, True,
                          xs[i + 1], ys[i + 1], NULL, NULL,
                          ICON_LABEL_MAX_WIDTH, ICON_LABEL_MAX_LINES);
            XtVaSetValues(ig, XmNshadowThickness, DESKTOP_ICON_SHADOW, NULL);
        }
    }

    free(names);
    free(xs);
    free(ys);

    icons_drawn = True;
    last_item_count = g_item_count;
    snapshot_dir_mtime();
}

static void select_by_name(const char *name)
{
    WidgetList children = NULL;
    Cardinal   num = 0;

    XtVaGetValues(gridContainer, XmNchildren, &children, XmNnumChildren, &num, NULL);

    for (Cardinal i = 0; i < num; i++) {
        char nm[256];
        entry_name_of(children[i], nm, sizeof(nm));
        if (strcmp(nm, name) == 0) {
            Widget sel[1];
            sel[0] = children[i];
            XtVaSetValues(gridContainer, XmNselectedObjects, sel,
                          XmNselectedObjectCount, 1, NULL);
            return;
        }
    }
}

static void refresh_keep_selection(void)
{
    char keep[256];
    snprintf(keep, sizeof(keep), "%s", selected_node_name);

    desktop_render();

    if (keep[0]) {
        snprintf(selected_node_name, sizeof(selected_node_name), "%s", keep);
        select_by_name(keep);
    }
}

static void tick_cb(XtPointer client, XtIntervalId *id)
{
    struct stat st;

    remember_positions();
    persist_if_changed();

    if (stat(current_path, &st) == 0 &&
        (st.st_mtim.tv_sec  != last_mtime.tv_sec ||
         st.st_mtim.tv_nsec != last_mtime.tv_nsec))
        refresh_keep_selection();

    XtAppAddTimeOut(XtWidgetToApplicationContext(toplevel_widget),
                    DESKTOP_POLL_MS, tick_cb, NULL);
}

/* =========================================================================
   ACTIONS
   ========================================================================= */

void desktop_open_folder(const char *path)
{
    char err[256];
    if (!spawn_file_manager(path, err, sizeof(err)))
        show_error_dialog(gridContainer, err);
}

void desktop_activate(const char *name)
{
    char full[PATH_MAX * 2];
    struct stat st;

    if (strcmp(name, DESKTOP_COMPUTER_ID) == 0) {
        desktop_open_folder("/");
        return;
    }

    join_path(full, sizeof(full), current_path, name);

    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (is_deskapp_bundle(full)) {
            char err[256];
            if (!launch_deskapp(full, err, sizeof(err)))
                show_error_dialog(gridContainer, err);
        } else {
            desktop_open_folder(full);
        }
    } else {
        open_with_default_app(full);
    }
}

/* =========================================================================
   CALLBACKS
   ========================================================================= */

static void desktop_select_cb(Widget w, XtPointer client, XtPointer call)
{
    XmContainerSelectCallbackStruct *cbs = (XmContainerSelectCallbackStruct *)call;

    selected_node_name[0] = '\0';
    if (cbs->selected_item_count > 0) {
        char nm[256];
        entry_name_of(cbs->selected_items[0], nm, sizeof(nm));
        if (strcmp(nm, DESKTOP_COMPUTER_ID) != 0)
            snprintf(selected_node_name, sizeof(selected_node_name), "%s", nm);
    }
}

static void desktop_default_action_cb(Widget w, XtPointer client, XtPointer call)
{
    XmContainerSelectCallbackStruct *cbs = (XmContainerSelectCallbackStruct *)call;
    char nm[256];

    if (cbs->selected_item_count < 1)
        return;

    entry_name_of(cbs->selected_items[0], nm, sizeof(nm));
    if (nm[0])
        desktop_activate(nm);
}

static void desktop_popup_cb(Widget w, XtPointer client, XtPointer call)
{
    XmPopupHandlerCallbackStruct *cbs = (XmPopupHandlerCallbackStruct *)call;
    Widget target = cbs->target;

    if (target && target != w && XmIsIconGadget(target)) {
        char nm[256];
        Widget sel[1];
        sel[0] = target;

        XtVaSetValues(gridContainer, XmNselectedObjects, sel,
                      XmNselectedObjectCount, 1, NULL);

        entry_name_of(target, nm, sizeof(nm));
        if (strcmp(nm, DESKTOP_COMPUTER_ID) == 0) {
            selected_node_name[0] = '\0';
            cbs->menuToPost = computer_popup;
        } else {
            snprintf(selected_node_name, sizeof(selected_node_name), "%s", nm);
            cbs->menuToPost = item_popup_menu;
        }
    } else {
        XtVaSetValues(gridContainer, XmNselectedObjectCount, 0, NULL);
        selected_node_name[0] = '\0';
        cbs->menuToPost = canvas_popup_menu_w;
    }

    cbs->postIt = True;
}

static void computer_open_cb(Widget w, XtPointer client, XtPointer call)
{
    desktop_open_folder("/");
}

/* =========================================================================
   WINDOW
   ========================================================================= */

static void desktop_settings(char *wallpaper, size_t wn, char *color, size_t cn)
{
    char path[PATH_MAX * 2];
    wallpaper[0] = color[0] = '\0';

    /* Checks user config first, falls back to bundle preferences/desktop.entry */
    if (prefs_read_file(path, sizeof(path))) {
        EntryDocument *doc = parse_entry_file(path);
        if (doc) {
            snprintf(wallpaper, wn, "%s", get_entry_string(doc, "Desktop", "Wallpaper", ""));
            snprintf(color,    cn, "%s", get_entry_string(doc, "Desktop", "Color", ""));
            free_entry_document(doc);
        }
    }

    if (color[0] == '\0') {
        snprintf(color, cn, "%s", "#4682B4");
    }

    if (wallpaper[0] == '~' && wallpaper[1] == '/' && home_dir()) {
        char tmp[PATH_MAX * 2];
        join_path(tmp, sizeof(tmp), home_dir(), wallpaper + 2);
        snprintf(wallpaper, wn, "%s", tmp);
    }
}

static void apply_appearance(Widget form, Widget container)
{
    char wallpaper[PATH_MAX * 2], color[64];
    desktop_settings(wallpaper, sizeof(wallpaper), color, sizeof(color));

    /* 1. Set Motif widget resources on both form and container */
    XtVaSetValues(form,
        XtVaTypedArg, XmNbackground, XmRString, color, (int)strlen(color) + 1,
        XtVaTypedArg, XmNforeground, XmRString, DESKTOP_TEXT_COLOR,
                      (int)strlen(DESKTOP_TEXT_COLOR) + 1,
        NULL);

    XtVaSetValues(container,
        XtVaTypedArg, XmNbackground, XmRString, color, (int)strlen(color) + 1,
        NULL);

    Display *dpy = XtDisplay(container);
    Window win = XtIsRealized(container) ? XtWindow(container) : None;
    Screen *scr = XtScreen(container);

    /* 2. Set raw X11 window background color on the container window so it paints immediately */
    if (color[0] && win != None) {
        XColor c0, c1;
        if (XAllocNamedColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), color, &c0, &c1)) {
            XSetWindowBackground(dpy, win, c0.pixel);
        }
    }

    /* 3. Load and set XPM wallpaper on both Motif and the raw X11 window of the container */
    if (wallpaper[0]) {
        Pixmap pm = None;
        int rc = XpmReadFileToPixmap(dpy, RootWindowOfScreen(scr),
                                     wallpaper, &pm, NULL, NULL);
        if (rc == XpmSuccess) {
            XtVaSetValues(container, XmNbackgroundPixmap, pm, NULL);
            if (win != None) {
                XSetWindowBackgroundPixmap(dpy, win, pm);
            }
        } else {
            fprintf(stderr, "desktop: cannot load wallpaper \"%s\" (%s)\n",
                    wallpaper, XpmGetErrorString(rc));
        }
    }

    /* 4. Force X11 to clear and redraw the window background instantly */
    if (win != None) {
        XClearWindow(dpy, win);
        XFlush(dpy);
    }
}

void desktop_build_ui(Widget toplevel)
{
    Screen *scr = XtScreen(toplevel);
    Arg args[16];
    int n;

    /* Initialize YeKara style engine on the top shell */
    YkResourceSysInit(toplevel);

    XtVaSetValues(toplevel,
        XmNtitle,               "Desktop",
        XmNx,                   0,
        XmNy,                   0,
        XmNwidth,               WidthOfScreen(scr),
        XmNheight,              HeightOfScreen(scr),
        XmNallowShellResize,    False,
        XmNdeleteResponse,      XmDO_NOTHING,
        XmNmappedWhenManaged,   False,
        XmNmwmDecorations,      0,
        XmNmwmFunctions,        0,
        NULL);

    YkReloadResources(toplevel);

    desktop_form = XmCreateForm(toplevel, "desktopForm", NULL, 0);

    XtManageChild(desktop_form);

    n = 0;
    XtSetArg(args[n], XmNtopAttachment,    XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment,   XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment,  XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNlayoutType,       XmSPATIAL);     n++;
    XtSetArg(args[n], XmNspatialStyle,     XmNONE);        n++;
    XtSetArg(args[n], XmNentryViewType,    XmLARGE_ICON);  n++;
    XtSetArg(args[n], XmNselectionPolicy,  XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNautomaticSelection, XmAUTO_SELECT); n++;
    XtSetArg(args[n], XmNshadowThickness,  0);              n++;
    XtSetArg(args[n], XmNmarginWidth,      0);              n++;
    XtSetArg(args[n], XmNmarginHeight,     0);              n++;
    /* Protect the grid container's spatial layout and visual properties */
    XtSetArg(args[n], XmNuserData,         YK_SKIP_THEME_FLAG); n++;

    gridContainer = XmCreateContainer(desktop_form, "desktopContainer", args, n);

    XtAddCallback(gridContainer, XmNselectionCallback,       desktop_select_cb,          NULL);
    XtAddCallback(gridContainer, XmNdefaultActionCallback, desktop_default_action_cb, NULL);
    XtAddCallback(gridContainer, XmNpopupHandlerCallback,   desktop_popup_cb,          NULL);
    XtManageChild(gridContainer);

    create_popup_menus(desktop_form);

    computer_popup = XmCreatePopupMenu(desktop_form, "computerPopupMenu", NULL, 0);
    Widget open = XtVaCreateManagedWidget("Open", xmPushButtonWidgetClass,
                                          computer_popup, NULL);
    XtAddCallback(open, XmNactivateCallback, computer_open_cb, NULL);
}

void desktop_realize(Widget toplevel)
{
    Display *dpy = XtDisplay(toplevel);

    XtRealizeWidget(toplevel);          /* Creates the X window */
    Window win = XtWindow(toplevel);

    XClassHint class_hint = {
        .res_name  = "yekara-desktop",
        .res_class = "YeKaraDesktop"
    };
    XSetClassHint(dpy, win, &class_hint);

    /* Apply appearance to both the form and gridContainer now that they are realized */
    apply_appearance(desktop_form, gridContainer);

    XtMapWidget(toplevel);
    XLowerWindow(dpy, win);
    XFlush(dpy);

    XtAppAddTimeOut(XtWidgetToApplicationContext(toplevel),
                    DESKTOP_POLL_MS, tick_cb, NULL);
}
