#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#include <Xm/Xm.h>
#include <X11/xpm.h>
#include <X11/Xlib.h>

/* libYekara: gzi_* image archive API, yk_find_filetype & friends.
 * If the file-type functions live in another yk header, add it here. */
#include <yk/gzi.h>
#include <yk/style.h>
#include <yk/bundle.h>
#include <yk/entry.h>

#include "icons.h"
#include "config.h"
#include "deskapp.h"
#include "state.h"
#include "util.h"

/* =========================================================================
   LOADING
   ========================================================================= */

void load_icon_gzi(Widget target_widget, const char *gzi_path,
                   const char *array_name, Pixmap *out, Pixmap *out_mask,
                   Boolean *valid)
{
    *valid = False;
    *out = None;
    *out_mask = None;

    GZI_Image *img = gzi_load(gzi_path, array_name);
    if (!img)
        return;

    char **xpm_data = gzi_build_xpm(img);
    gzi_free(img);
    if (!xpm_data)
        return;

    Display *dpy = XtDisplay(target_widget);
    Window root = RootWindowOfScreen(XtScreen(target_widget));

    XpmAttributes attr;
    memset(&attr, 0, sizeof(attr));
    attr.visual   = DefaultVisualOfScreen(XtScreen(target_widget));
    attr.colormap = DefaultColormapOfScreen(XtScreen(target_widget));
    attr.depth    = DefaultDepthOfScreen(XtScreen(target_widget));
    attr.valuemask = XpmVisual | XpmColormap | XpmDepth;

    int rc = XpmCreatePixmapFromData(dpy, root, xpm_data, out, out_mask, &attr);
    *valid = (rc == XpmSuccess);

    for (int i = 0; xpm_data[i] != NULL; i++)
        free(xpm_data[i]);
    free(xpm_data);
}

/*
 * Resolve built-in icon archives relative to the running executable
 * (<bundle>/filecabinet -> <bundle>/resources/), so it works no matter
 * what working directory the launcher used. Falls back to ./resources.
 */
static void app_resource_path(char *out, size_t n, const char *file)
{
    static char res_dir[PATH_MAX] = "";

    if (res_dir[0] == '\0') {
        char exe[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);

        if (len > 0) {
            exe[len] = '\0';
            char *slash = strrchr(exe, '/');
            if (slash) {
                *slash = '\0';
                join_path(res_dir, sizeof(res_dir), exe, DESKAPP_RESOURCES_DIR);
            }
        }
        if (res_dir[0] == '\0')
            snprintf(res_dir, sizeof(res_dir), "%s", DESKAPP_RESOURCES_DIR);
    }

    join_path(out, n, res_dir, file);
}

void init_icons(Widget w)
{
    char folder_gzi[PATH_MAX * 2];
    char misc_gzi[PATH_MAX * 2];

    app_resource_path(folder_gzi, sizeof(folder_gzi), ICON_ARCHIVE);
    app_resource_path(misc_gzi,   sizeof(misc_gzi),   ICON_ALT_ARCHIVE);

    load_icon_gzi(w, folder_gzi, "folder_normal", &iconFolder,    &iconFolderMask,    &iconFolderValid);
    load_icon_gzi(w, misc_gzi,   "file",          &iconFile,      &iconFileMask,      &iconFileValid);
    load_icon_gzi(w, misc_gzi,   "computer",      &iconComputer,  &iconComputerMask,  &iconComputerValid);
    load_icon_gzi(w, folder_gzi, "folder_up",     &iconUp,        &iconUpMask,        &iconUpValid);
    load_icon_gzi(w, folder_gzi, "folder_big",    &iconFolderBig, &iconFolderBigMask, &iconFolderBigValid);
}


/* =========================================================================
   PER-BUNDLE ICON CACHE
   ========================================================================= */

#define BUNDLE_ICON_CACHE_MAX 256

typedef struct {
    char         bundle_path[PATH_MAX];
    Pixmap       icon, mask;
    Boolean      valid;
    unsigned int width, height;
} BundleIconCacheEntry;

static BundleIconCacheEntry bundle_icon_cache[BUNDLE_ICON_CACHE_MAX];
static int bundle_icon_cache_count = 0;

static BundleIconCacheEntry *find_bundle_icon_cache(const char *bundle_path)
{
    for (int i = 0; i < bundle_icon_cache_count; i++)
        if (strcmp(bundle_icon_cache[i].bundle_path, bundle_path) == 0)
            return &bundle_icon_cache[i];
    return NULL;
}

static BundleIconCacheEntry *get_bundle_icon_cache(const char *bundle_path)
{
    BundleIconCacheEntry *e = find_bundle_icon_cache(bundle_path);
    if (e)
        return e;

    if (bundle_icon_cache_count >= BUNDLE_ICON_CACHE_MAX)
        return NULL;

    e = &bundle_icon_cache[bundle_icon_cache_count++];
    memset(e, 0, sizeof(*e));
    snprintf(e->bundle_path, sizeof(e->bundle_path), "%s", bundle_path);

    char rel_file[PATH_MAX];
    char tag[128];

    if (deskapp_read_icon(bundle_path, rel_file, sizeof(rel_file),
                          tag, sizeof(tag)) != 0)
        return e;

    /* Ensure the relative resource filename ends with .gzi */
    if (!ends_with(rel_file, ".gzi"))
        strcat(rel_file, ".gzi");

    if (!bundle_relative_path_is_safe(rel_file)) {
        fprintf(stderr, "%s: Icon= path \"%s\" is unsafe - ignoring.\n",
                bundle_path, rel_file);
        return e;
    }

    char resources_dir[PATH_MAX];
    char full_gzi[PATH_MAX * 2];

    join_path(resources_dir, sizeof(resources_dir), bundle_path,
              DESKAPP_RESOURCES_DIR);
    join_path(full_gzi, sizeof(full_gzi), resources_dir, rel_file);

    if (!path_exists(full_gzi)) {
        fprintf(stderr, "%s: Icon= resolved to \"%s\" (not found) - ignoring.\n",
                bundle_path, full_gzi);
        return e;
    }

    load_icon_gzi(gridContainer, full_gzi, tag, &e->icon, &e->mask, &e->valid);

    if (e->valid) {
        Window       root;
        int          gx, gy;
        unsigned int gw, gh, border, depth;

        if (XGetGeometry(XtDisplay(gridContainer), e->icon, &root,
                         &gx, &gy, &gw, &gh, &border, &depth)) {
            e->width  = gw;
            e->height = gh;
        } else {
            e->valid = False;
        }
    }

    return e;
}


/* =========================================================================
   FILE-TYPE ICON CACHE
   (previously every render re-created a pixmap per file and leaked it)
   ========================================================================= */

#define FILETYPE_ICON_CACHE_MAX 128

typedef struct {
    char    key[2048];
    Pixmap  icon, mask;
    Boolean valid;
} FileTypeIconEntry;

static FileTypeIconEntry filetype_icon_cache[FILETYPE_ICON_CACHE_MAX];
static int filetype_icon_cache_count = 0;

static void load_icon_cached(const char *gzi_path, const char *tag,
                             Pixmap *icon, Pixmap *mask, Boolean *valid)
{
    char key[2048];
    snprintf(key, sizeof(key), "%s|%s", gzi_path, tag);

    for (int i = 0; i < filetype_icon_cache_count; i++) {
        if (strcmp(filetype_icon_cache[i].key, key) == 0) {
            *icon  = filetype_icon_cache[i].icon;
            *mask  = filetype_icon_cache[i].mask;
            *valid = filetype_icon_cache[i].valid;
            return;
        }
    }

    if (filetype_icon_cache_count >= FILETYPE_ICON_CACHE_MAX) {
        *icon = *mask = None;
        *valid = False;          /* caller falls back to the generic icon */
        return;
    }

    FileTypeIconEntry *e = &filetype_icon_cache[filetype_icon_cache_count++];
    snprintf(e->key, sizeof(e->key), "%s", key);
    load_icon_gzi(gridContainer, gzi_path, tag, &e->icon, &e->mask, &e->valid);

    *icon = e->icon;  *mask = e->mask;  *valid = e->valid;
}


/* =========================================================================
   ICON SELECTION
   ========================================================================= */

void resolve_icon(int index, Pixmap *icon, Pixmap *mask,
                  Boolean *valid, unsigned int *w, unsigned int *h)
{
    *w = *h = ICON_VIEW_ICON_SIZE;

    if (index == 0) {
        *icon = iconUp;  *mask = iconUpMask;  *valid = iconUpValid;
    } else if (g_items[index - 1].is_directory) {
        *icon = iconFolder;  *mask = iconFolderMask;  *valid = iconFolderValid;
    } else if (g_items[index - 1].is_app) {
        char bundle_full[PATH_MAX * 2];
        join_path(bundle_full, sizeof(bundle_full), current_path,
                  g_items[index - 1].name);

        BundleIconCacheEntry *bi = get_bundle_icon_cache(bundle_full);

        if (bi && bi->valid) {
            *icon = bi->icon;  *mask = bi->mask;  *valid = True;
            *w = bi->width;    *h = bi->height;
        } else {
            *icon = iconFile;  *mask = iconFileMask;  *valid = iconFileValid;
        }
    } else {
        char ext[32];
        if (yk_file_extension(g_items[index - 1].name, ext, sizeof(ext)) == 0) {
            ykFileType ft;
            if (yk_find_filetype(ext, &ft) == 0 && ft.icon_file[0] != '\0') {
                load_icon_cached(ft.icon_file, ft.icon_tag, icon, mask, valid);
                if (*valid)
                    return;
            }
        }
        *icon = iconFile;  *mask = iconFileMask;  *valid = iconFileValid;
    }
}
