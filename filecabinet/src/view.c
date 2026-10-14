#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>

#include <Xm/Xm.h>
#include <Xm/Container.h>
#include <Xm/IconG.h>

#include "view.h"
#include "config.h"
#include "deskapp.h"
#include "entries.h"
#include "fsops.h"
#include "icons.h"
#include "state.h"
#include "util.h"

/* Spatial (icon view) layout memory, per directory. */
static IconPosition icon_positions[MAX_ITEMS];
static int          icon_position_count = 0;
static char         icon_view_path[PATH_MAX] = "";
static Boolean      container_spatial = False;


/* =========================================================================
   STATUS BAR
   ========================================================================= */

void update_status_bar(void)
{
    if (!statusLabel)
        return;

    char status_text[PATH_MAX + 150];

    if (strlen(selected_node_name) > 0) {
        char full_path[PATH_MAX * 2];
        struct stat st;

        join_path(full_path, sizeof(full_path), current_path, selected_node_name);

        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(status_text, sizeof(status_text),
                     "Selected: %s  (%s)", selected_node_name,
                     is_deskapp_bundle(full_path) ? "Application" : "Folder");
        } else if (stat(full_path, &st) == 0) {
            char sz[32];
            format_size(st.st_size, sz, sizeof(sz));
            snprintf(status_text, sizeof(status_text),
                     "Selected: %s  (%s)", selected_node_name, sz);
        } else {
            snprintf(status_text, sizeof(status_text),
                     "Selected: %s", selected_node_name);
        }

    } else if (is_moving_file || is_copying_file) {
        char *filename = strrchr(source_move_path, '/');
        snprintf(status_text, sizeof(status_text),
                 "%s on clipboard: %s  (Edit > Paste)",
                 is_moving_file ? "Cut" : "Copied",
                 filename ? filename + 1 : source_move_path);
    } else {
        snprintf(status_text, sizeof(status_text),
                 "%d Item(s) Found%s", last_item_count,
                 show_hidden ? "  (showing hidden)" : "");
    }

    XmString statusStr = XmStringCreateLocalized(status_text);
    XtVaSetValues(statusLabel, XmNlabelString, statusStr, NULL);
    XmStringFree(statusStr);
}


/* =========================================================================
   ICON POSITIONS
   ========================================================================= */

static void save_icon_position(const char *name, int x, int y)
{
    for (int i = 0; i < icon_position_count; i++) {
        if (strcmp(icon_positions[i].name, name) == 0) {
            icon_positions[i].x = x;
            icon_positions[i].y = y;
            return;
        }
    }

    if (icon_position_count < MAX_ITEMS) {
        IconPosition *p = &icon_positions[icon_position_count];
        snprintf(p->name, sizeof(p->name), "%s", name);
        p->x = x;
        p->y = y;
        icon_position_count++;
    }
}

static void get_icon_position(const char *name, int default_x, int default_y,
                              int *out_x, int *out_y)
{
    for (int i = 0; i < icon_position_count; i++) {
        if (strcmp(icon_positions[i].name, name) == 0) {
            *out_x = icon_positions[i].x;
            *out_y = icon_positions[i].y;
            return;
        }
    }

    *out_x = default_x;
    *out_y = default_y;
    save_icon_position(name, default_x, default_y);
}

static void save_spatial_positions(void)
{
    if (!container_spatial || strcmp(current_path, icon_view_path) != 0) {
        container_spatial = False;
        return;
    }

    WidgetList children = NULL;
    Cardinal   num = 0;

    XtVaGetValues(gridContainer, XmNchildren, &children, XmNnumChildren, &num, NULL);

    for (Cardinal i = 0; i < num; i++) {
        XtPointer nm = NULL;
        Position  x = 0, y = 0;

        XtVaGetValues(children[i], XmNuserData, &nm, XmNx, &x, XmNy, &y, NULL);
        if (nm)
            save_icon_position((const char *)nm, (int)x, (int)y);
    }

    container_spatial = False;
}


/* =========================================================================
   VIEWPORT
   ========================================================================= */

static Boolean get_viewport_size(Dimension *w, Dimension *h)
{
    Widget clip = NULL;
    *w = *h = 0;
    if (!scrolledWindowFrame)
        return False;

    XtVaGetValues(scrolledWindowFrame, XmNclipWindow, &clip, NULL);
    if (!clip)
        return False;

    XtVaGetValues(clip, XmNwidth, w, XmNheight, h, NULL);
    return (*w > 20 && *h > 20) ? True : False;
}

static void fit_container_size(void)
{
    Dimension vw, vh;
    if (!gridContainer || !get_viewport_size(&vw, &vh))
        return;

    WidgetList children = NULL;
    Cardinal   num = 0;
    int max_right = 0, max_bottom = 0;

    XtVaGetValues(gridContainer, XmNchildren, &children, XmNnumChildren, &num, NULL);

    for (Cardinal i = 0; i < num; i++) {
        Position  x = 0, y = 0;
        Dimension w = 0, h = 0;

        if (!XtIsManaged(children[i]))
            continue;

        XtVaGetValues(children[i], XmNx, &x, XmNy, &y, XmNwidth, &w, XmNheight, &h, NULL);

        if (use_icon_view) {
            if (w < ICON_VIEW_ITEM_WIDTH)  w = ICON_VIEW_ITEM_WIDTH;
            if (h < ICON_VIEW_ITEM_HEIGHT) h = ICON_VIEW_ITEM_HEIGHT;
        }

        if (x + (int)w > max_right)  max_right  = x + (int)w;
        if (y + (int)h > max_bottom) max_bottom = y + (int)h;
    }

    int want_w = max_right  + ICON_VIEW_MARGIN;
    int want_h = max_bottom + ICON_VIEW_MARGIN;

    if (want_w < (int)vw) want_w = (int)vw;
    if (want_h < (int)vh) want_h = (int)vh;

    XtVaSetValues(gridContainer, XmNwidth, (Dimension)want_w, XmNheight, (Dimension)want_h, NULL);
}

static void reflow_icons(Dimension vw)
{
    WidgetList children = NULL;
    Cardinal   num = 0;

    int container_width = (int)vw - (VIEW_MARGIN * 2);
    int columns = (container_width - ICON_VIEW_MARGIN) /
                  (ICON_VIEW_ITEM_WIDTH + ICON_VIEW_SPACING);
    if (columns < 1)
        columns = 1;

    icon_position_count = 0;

    XtVaGetValues(gridContainer, XmNchildren, &children, XmNnumChildren, &num, NULL);

    int slot = 0;
    for (Cardinal i = 0; i < num; i++) {
        if (!XtIsManaged(children[i]))
            continue;

        int col = slot % columns;
        int row = slot / columns;
        slot++;

        int x = ICON_VIEW_MARGIN + col * (ICON_VIEW_ITEM_WIDTH + ICON_VIEW_SPACING);
        int y = ICON_VIEW_MARGIN + row * (ICON_VIEW_ITEM_HEIGHT + ICON_VIEW_SPACING);

        XtVaSetValues(children[i], XmNx, (Position)x, XmNy, (Position)y, NULL);
    }
}

void viewport_resize_handler(Widget w, XtPointer client,
                             XEvent *event, Boolean *cont)
{
    static Dimension last_vw = 0;
    Dimension vw, vh;

    if (event->type != ConfigureNotify)
        return;

    if (get_viewport_size(&vw, &vh)) {
        if (vw != last_vw && use_icon_view && container_spatial)
            reflow_icons(vw);
        last_vw = vw;
    }

    fit_container_size();
}


/* =========================================================================
   BUILD & RENDER
   ========================================================================= */

static void build_entries(Dimension container_width)
{
    Boolean spatial = use_icon_view;

    if (spatial) {
        if (strcmp(current_path, icon_view_path) != 0) {
            icon_position_count = 0;
            snprintf(icon_view_path, sizeof(icon_view_path), "%s", current_path);
        }

        XtVaSetValues(gridContainer,
            XmNlayoutType,    XmSPATIAL,
            XmNspatialStyle,  XmNONE,
            XmNentryViewType, XmLARGE_ICON,
            NULL);
    } else {
        XtVaSetValues(gridContainer,
            XmNlayoutType,    XmDETAIL,
            XmNentryViewType, XmSMALL_ICON,
            NULL);
    }

    int columns = (int)((container_width - ICON_VIEW_MARGIN) /
                        (ICON_VIEW_ITEM_WIDTH + ICON_VIEW_SPACING));
    if (columns < 1)
        columns = 1;

    for (int index = 0; index <= g_item_count; index++) {
        const char *name    = (index == 0) ? ".." : g_items[index - 1].name;
        const char *caption = (index == 0) ? ".. (go up)" : name;

        Pixmap       icon, mask;
        Boolean      valid;
        unsigned int iw, ih;

        resolve_icon(index, &icon, &mask, &valid, &iw, &ih);

        if (spatial) {
            int col = index % columns;
            int row = index / columns;

            int default_x = ICON_VIEW_MARGIN + col * (ICON_VIEW_ITEM_WIDTH + ICON_VIEW_SPACING);
            int default_y = ICON_VIEW_MARGIN + row * (ICON_VIEW_ITEM_HEIGHT + ICON_VIEW_SPACING);
            int x, y;

            get_icon_position(name, default_x, default_y, &x, &y);

            create_entry(gridContainer, name, caption, icon, mask, valid,
                         XmLARGE_ICON, True, x, y, NULL, NULL,
                         ICON_LABEL_MAX_WIDTH, ICON_LABEL_MAX_LINES);
        } else {
            char sz[32] = "", dt[32] = "";
            if (index > 0)
                describe_item(&g_items[index - 1], sz, sizeof(sz), dt, sizeof(dt));

            create_entry(gridContainer, name, caption, icon, mask, valid,
                         XmSMALL_ICON, False, 0, 0, sz, dt,
                         LIST_NAME_MAX_WIDTH, 1);
        }
    }

    container_spatial = spatial;
}

/* =========================================================================
   BREADCRUMBS
   ========================================================================= */

#define CRUMB_STEP   (URL_ITEM_WIDTH + 8)   /* width of one folder crumb  */
#define CRUMB_SEP    16                     /* width of the ">" separator */
#define CRUMB_ELL_W  40                     /* width of the "..." marker  */
#define CRUMB_MAX    1024

static void add_breadcrumb_entry(const char *label, const char *path,
                                 Pixmap icon, Pixmap mask, Boolean valid,
                                 int x, int y)
{
    create_entry(breadcrumbRow, path, label, icon, mask, valid,
                 XmLARGE_ICON, True, x, y, NULL, NULL,
                 BREADCRUMB_LABEL_MAX_WIDTH, BREADCRUMB_LABEL_MAX_LINES);
}

static void add_breadcrumb_text(const char *text, int x, int y)
{
    create_entry(breadcrumbRow, "", text, None, None, False,
                 XmLARGE_ICON, True, x,
                 y + (URL_ICON_SIZE / 2) - 6, NULL, NULL, 0, 0);
}

static int breadcrumb_row_width(void)
{
    Dimension w = 0;
    if (breadcrumbRow)
        XtVaGetValues(breadcrumbRow, XmNwidth, &w, NULL);
    return (w > 100) ? (int)w : 740;   /* not realized yet: assume default */
}

/*
 * Rebuilds the breadcrumb bar. When the path is too long for the bar, the
 * leading folders are replaced by a "..." marker so the current folder is
 * always visible (it used to run off the right edge).
 */
static void rebuild_breadcrumbs(void)
{
    if (!breadcrumbRow)
        return;

    XtVaSetValues(breadcrumbRow, XmNselectedObjectCount, 0, NULL);
    destroy_all_children(breadcrumbRow);

    /* split current_path (already realpath'd) into components */
    int starts[CRUMB_MAX], lens[CRUMB_MAX], n = 0;
    const char *p = current_path;
    while (*p && n < CRUMB_MAX) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *s = p;
        while (*p && *p != '/')
            p++;
        starts[n] = (int)(s - current_path);
        lens[n]   = (int)(p - s);
        n++;
    }

    int avail = breadcrumb_row_width() - (URL_MARGIN * 2) - 12;
    int first = 0;

    if (CRUMB_STEP + n * (CRUMB_SEP + CRUMB_STEP) > avail) {
        for (first = 1; first < n - 1; first++) {
            int used = CRUMB_STEP + CRUMB_SEP + CRUMB_ELL_W +
                       (n - first) * (CRUMB_SEP + CRUMB_STEP);
            if (used <= avail)
                break;
        }
        if (n < 2)
            first = 0;
    }

    int url_x = URL_MARGIN + 6;
    int url_y = URL_MARGIN + 2;

    add_breadcrumb_entry("/", "/", iconComputer, iconComputerMask, iconComputerValid, url_x, url_y);
    url_x += CRUMB_STEP;

    if (first > 0) {
        add_breadcrumb_text(">", url_x, url_y);
        url_x += CRUMB_SEP;
        add_breadcrumb_text("...", url_x, url_y);
        url_x += CRUMB_ELL_W;
    }

    for (int i = first; i < n; i++) {
        char name[256];
        char prefix[PATH_MAX];

        add_breadcrumb_text(">", url_x, url_y);
        url_x += CRUMB_SEP;

        snprintf(name,   sizeof(name),   "%.*s", lens[i], current_path + starts[i]);
        snprintf(prefix, sizeof(prefix), "%.*s", starts[i] + lens[i], current_path);

        add_breadcrumb_entry(name, prefix,
                             iconFolderBig, iconFolderBigMask, iconFolderBigValid,
                             url_x, url_y);
        url_x += CRUMB_STEP;
    }
}

void breadcrumb_resize_handler(Widget w, XtPointer client,
                               XEvent *event, Boolean *cont)
{
    static Dimension last_w = 0;
    Dimension cw = 0;

    if (event->type != ConfigureNotify)
        return;

    XtVaGetValues(w, XmNwidth, &cw, NULL);
    if (cw != last_w) {
        last_w = cw;
        rebuild_breadcrumbs();
    }
}


void render_view(void)
{
    save_spatial_positions();
    selected_node_name[0] = '\0';

    XtVaSetValues(gridContainer,  XmNselectedObjectCount, 0, NULL);

    destroy_all_children(gridContainer);
    rebuild_breadcrumbs();

    free(g_items);
    g_item_count = scan_directory(current_path, &g_items);

    Dimension container_width = 720;
    {
        Dimension vw, vh;
        if (get_viewport_size(&vw, &vh))
            container_width = vw - (VIEW_MARGIN * 2);
    }
    if (container_width < 200)
        container_width = 720;

    build_entries(container_width);
    fit_container_size();

    last_item_count = g_item_count;

    if (toplevel_widget) {
        char title[PATH_MAX + 32];
        snprintf(title, sizeof(title), "File Manager - %s", current_path);
        XtVaSetValues(toplevel_widget, XmNtitle, title, NULL);
    }

    update_status_bar();
}
