#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include "yk/bundle.h"
#include "yk/entry.h" // Include your unified entry parser header

#define MAX_APPS 256

#define YK_APPS_PATH_ENV      "YK_APPS_PATH"
#define YK_APPS_PATH_DEFAULT  "/applications:$HOME/applications"

#define BUNDLE_MAX_TYPES      64     /* file type sections read per bundle */
#define SCAN_MAX_DEPTH        16


// ------------------------------------------------
// Small helpers
// ------------------------------------------------

static int yk_verbose = 0;

void yk_set_verbose(int on) {
    yk_verbose = on;
}

static void yk_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void yk_warn(const char *fmt, ...) {
    va_list ap;

    if (!yk_verbose)
        return;

    fprintf(stderr, "yk: warning: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* Letters, digits, '_' and '-' only (non-empty). */
static int valid_token(const char *s) {
    if (!*s)
        return 0;

    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-')
            return 0;

    return 1;
}

/* ".TXT" / "txt" -> "txt". 0 if empty, too long or odd characters. */
static int normalize_ext(const char *in, char *out, size_t n) {
    if (*in == '.')
        in++;

    size_t len = strlen(in);
    if (len == 0 || len >= n)
        return 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (!isalnum(c) && c != '_' && c != '-' && c != '+')
            return 0;
        out[i] = (char)tolower(c);
    }
    out[len] = '\0';
    return 1;
}

/* Must be relative, with no ".." component anywhere in it. */
static int relative_path_is_safe(const char *rel) {
    if (rel[0] == '/')
        return 0;

    const char *p = rel;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);

        if (len == 2 && p[0] == '.' && p[1] == '.')
            return 0;
        if (!slash)
            break;
        p = slash + 1;
    }
    return 1;
}

/* Case-insensitive lookup of a key inside [Application Entry]. */
static const EntryNode *find_app_node(const EntryDocument *doc, const char *key)
{
    for (const EntrySection *s = doc->sections; s; s = s->next) {
        if (strcasecmp(s->name, "Application Entry") != 0)
            continue;
        for (const EntryNode *n = s->entries; n; n = n->next)
            if (strcasecmp(n->key, key) == 0)
                return n;
    }
    return NULL;
}

/* "a", "b", "c" -> "a:b:c" (truncates safely, always NUL-terminated). */
static void join_values(const EntryNode *n, char *out, size_t out_n)
{
    size_t used = 0;

    if (out_n == 0)
        return;
    out[0] = '\0';

    for (int i = 0; i < n->value_count; i++) {
        int w = snprintf(out + used, out_n - used, "%s%s",
                         i ? ":" : "", n->values[i]);
        if (w < 0 || (size_t)w >= out_n - used)
            return;
        used += (size_t)w;
    }
}

/*
 * Helper using the entry library's multi-value parsing for icons.
 */
static int build_icon_path_from_entry(const char *bundle_path, const EntryNode *node,
                                      char *out, size_t out_n)
{
    char file[PATH_MAX_LEN];
    const char *tag = node->value_count > 1 ? node->values[1] : "";

    if (snprintf(file, sizeof(file), "%s", node->values[0]) >= (int)sizeof(file))
        return 0;

    size_t fl = strlen(file);
    if (fl == 0)
        return 0;

    if (fl < 4 || strcmp(file + fl - 4, ".gzi") != 0) {
        if (fl + 5 > sizeof(file))
            return 0;
        strcat(file, ".gzi");
    }

    if (!relative_path_is_safe(file))
        return 0;

    int n;
    if (tag[0])
        n = snprintf(out, out_n, "%s/resources/%s:%s", bundle_path, file, tag);
    else
        n = snprintf(out, out_n, "%s/resources/%s", bundle_path, file);

    return n > 0 && (size_t)n < out_n;
}


// ------------------------------------------------
// Directory scanning (shared by apps and file types)
// ------------------------------------------------

typedef int (*BundleVisitor)(const char *bundle_path, void *user);

static int is_deskapp_name(const char *name) {
    size_t len = strlen(name);
    return len > 8 && strcasecmp(name + len - 8, ".deskapp") == 0;
}

static int scan_bundles(const char *dir_path, BundleVisitor visit,
                        void *user, int depth)
{
    if (depth > SCAN_MAX_DEPTH)
        return 1;

    struct dirent **list = NULL;
    int cnt = scandir(dir_path, &list, NULL, alphasort);
    if (cnt < 0)
        return 1;

    int keep_going = 1;

    for (int i = 0; i < cnt; i++) {
        const struct dirent *entry = list[i];

        if (keep_going &&
            strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {

            char full_path[PATH_MAX_LEN];
            snprintf(full_path, PATH_MAX_LEN, "%s/%s", dir_path, entry->d_name);

            int is_dir = 0, is_link = 0;

            if (entry->d_type == DT_DIR) {
                is_dir = 1;
            } else if (entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN) {
                struct stat st;
                if (lstat(full_path, &st) == 0) {
                    is_link = S_ISLNK(st.st_mode);
                    if (is_link)
                        is_dir = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));
                    else
                        is_dir = S_ISDIR(st.st_mode);
                }
            }

            if (is_dir) {
                if (is_deskapp_name(entry->d_name))
                    keep_going = visit(full_path, user);
                else if (!is_link)
                    keep_going = scan_bundles(full_path, visit, user, depth + 1);
            }
        }

        free(list[i]);
    }

    free(list);
    return keep_going;
}

static void expand_dir(const char *in, char *out, size_t n) {
    const char *home = getenv("HOME");
    if (!home)
        home = ".";

    if (strncmp(in, "${HOME}", 7) == 0 && (in[7] == '/' || in[7] == '\0'))
        snprintf(out, n, "%s%s", home, in + 7);
    else if (strncmp(in, "$HOME", 5) == 0 && (in[5] == '/' || in[5] == '\0'))
        snprintf(out, n, "%s%s", home, in + 5);
    else if (in[0] == '~')
        snprintf(out, n, "%s%s", home, in + 1);
    else
        snprintf(out, n, "%s", in);
}

static void scan_path_list(const char *path_list, BundleVisitor visit,
                           void *user)
{
    char  copy[4096];
    char *save = NULL;

    snprintf(copy, sizeof(copy), "%s", path_list);

    for (char *tok = strtok_r(copy, ":", &save); tok;
         tok = strtok_r(NULL, ":", &save)) {

        char dir_path[PATH_MAX_LEN];
        expand_dir(tok, dir_path, sizeof(dir_path));

        if (!scan_bundles(dir_path, visit, user, 0))
            return;
    }
}


// ------------------------------------------------
// App listing
// ------------------------------------------------

typedef struct {
    ykApp *apps;
    int    count;
} ListCtx;

static int list_apps_visit(const char *bundle_path, void *user) {
    ListCtx *c = user;

    if (c->count >= MAX_APPS)
        return 0;

    if (yk_parse(bundle_path, &c->apps[c->count]) == 0)
        c->count++;

    return c->count < MAX_APPS;
}

int yk_list_apps(const char *env_var, ykApp apps[], int *app_count) {
    char *path_env = getenv(env_var);
    if (!path_env) {
        fprintf(stderr, "Environment variable '%s' not set\n", env_var);
        return 1;
    }

    ListCtx ctx = { apps, 0 };
    scan_path_list(path_env, list_apps_visit, &ctx);

    *app_count = ctx.count;
    return 0;
}


// ------------------------------------------------
// Metadata parsing using entry.h
// ------------------------------------------------

int yk_parse(const char *bundle_path, ykApp *app) {
    char meta_path[PATH_MAX_LEN];
    snprintf(meta_path, PATH_MAX_LEN, "%s/app.metadata", bundle_path);

    EntryDocument *doc = parse_entry_file(meta_path);
    if (!doc) {
        fprintf(stderr, "Error: Metadata not found in '%s'\n", bundle_path);
        return 1;
    }

    const EntryNode *exec_node = find_app_node(doc, "ExecFile");
    const char *exec_file = exec_node ? exec_node->values[0] : "";
    const EntryNode *icon_node = find_app_node(doc, "Icon");

    if (!exec_file || exec_file[0] == '\0') {
        fprintf(stderr, "Error: ExecFile missing in metadata\n");
        free_entry_document(doc);
        return 2;
    }

    snprintf(app->exec_path, PATH_MAX_LEN, "%s/%s", bundle_path, exec_file);
    snprintf(app->bundle_path, PATH_MAX_LEN, "%s", bundle_path);

    app->icon_path[0] = '\0';
    if (icon_node) {
        build_icon_path_from_entry(bundle_path, icon_node, app->icon_path, PATH_MAX_LEN);
    }

    const char *last_slash = strrchr(bundle_path, '/');
    if (last_slash)
        snprintf(app->name, NAME_MAX_LEN, "%s", last_slash + 1);
    else
        snprintf(app->name, NAME_MAX_LEN, "%s", bundle_path);

    free_entry_document(doc);
    return 0;
}


// ------------------------------------------------
// Metadata property reader using entry.h
// ------------------------------------------------

int yk_get_property(const char *bundle_path,
                    const char *key,
                    char *out_value,
                    size_t out_size)
{
    if (!out_value || out_size == 0)
        return 2;

    char meta_path[PATH_MAX_LEN];
    snprintf(meta_path, PATH_MAX_LEN, "%s/app.metadata", bundle_path);

    EntryDocument *doc = parse_entry_file(meta_path);
    if (!doc) return 1;

    const EntryNode *node = find_app_node(doc, key);
    if (!node) {
        free_entry_document(doc);
        return 2;
    }

    join_values(node, out_value, out_size);   /* multi-value -> "a:b:c" */
    free_entry_document(doc);
    return 0;
}


// ------------------------------------------------
// Find specific app
// ------------------------------------------------

int yk_find_app(const char *app_name, ykApp *out_app) {
    ykApp apps[MAX_APPS];
    int count = 0;

    if (yk_list_apps("YK_APPS_PATH", apps, &count) != 0)
        return 2;

    for (int i = 0; i < count; i++) {
        if (strcmp(apps[i].name, app_name) == 0) {
            *out_app = apps[i];
            return 0;
        }
    }

    char name_with_ext[NAME_MAX_LEN];
    snprintf(name_with_ext, NAME_MAX_LEN, "%s.deskapp", app_name);

    for (int i = 0; i < count; i++) {
        if (strcmp(apps[i].name, name_with_ext) == 0) {
            *out_app = apps[i];
            return 0;
        }
    }

    return 1;
}


// ------------------------------------------------
// File types using entry.h structures
// ------------------------------------------------

typedef struct {
    const char *bundle;
    char        exec_rel[PATH_MAX_LEN];
    ykFileType *types;
    int         ntypes;
} FtScanCtx;

static int bundle_filetypes(const char *bundle, ykFileType *types) {
    char meta_path[PATH_MAX_LEN];
    snprintf(meta_path, PATH_MAX_LEN, "%s/app.metadata", bundle);

    EntryDocument *doc = parse_entry_file(meta_path);
    if (!doc)
        return 0;

    FtScanCtx s;
    memset(&s, 0, sizeof(s));
    s.bundle = bundle;
    s.types  = types;

    EntrySection *sec = doc->sections;
    while (sec) {
        if (strcmp(sec->name, "global") == 0 || strcasecmp(sec->name, "Application Entry") == 0) {
            if (strcasecmp(sec->name, "Application Entry") == 0) {
                EntryNode *n = sec->entries;
                while (n) {
                    if (strcasecmp(n->key, "ExecFile") == 0 && !s.exec_rel[0]) {
                        snprintf(s.exec_rel, sizeof(s.exec_rel), "%s", n->values[0]);
                    }
                    n = n->next;
                }
            }
            sec = sec->next;
            continue;
        }

        // Strip " Entry" case-insensitively before normalizing extension
        char sec_copy[64];
        snprintf(sec_copy, sizeof(sec_copy), "%s", sec->name);
        size_t slen = strlen(sec_copy);
        if (slen > 6 && strcasecmp(sec_copy + slen - 6, " Entry") == 0) {
            sec_copy[slen - 6] = '\0';
        }

        char ext[YK_EXT_MAX];
        if (!normalize_ext(sec_copy, ext, sizeof(ext))) {
            yk_warn("%s: bad file type name \"%s\" - ignoring that section.", bundle, sec->name);
            sec = sec->next;
            continue;
        }

        int duplicate = 0;
        for (int i = 0; i < s.ntypes; i++) {
            if (strcmp(s.types[i].ext, ext) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate || s.ntypes >= BUNDLE_MAX_TYPES) {
            sec = sec->next;
            continue;
        }

        ykFileType *t = &s.types[s.ntypes++];
        memset(t, 0, sizeof(*t));
        snprintf(t->ext, sizeof(t->ext), "%s", ext);
        snprintf(t->handler, sizeof(t->handler), "open");
        snprintf(t->bundle_path, sizeof(t->bundle_path), "%s", bundle);

        EntryNode *n = sec->entries;
        while (n) {
            if (strcasecmp(n->key, "icon") == 0) {
                char file[PATH_MAX_LEN];
                char tag[NAME_MAX_LEN];

                if (n->value_count < 2 || !n->values[1][0]) {
                    yk_warn("%s: [%s Entry] icon must look like \"file.gzi:tag;\" - ignoring.", bundle, t->ext);
                } else {
                    snprintf(file, sizeof(file), "%s", n->values[0]);
                    snprintf(tag, sizeof(tag), "%s", n->values[1]);

                    // Ensure the icon filename ends with .gzi
                    size_t fl = strlen(file);
                    if (fl < 4 || strcmp(file + fl - 4, ".gzi") != 0) {
                        if (fl + 5 <= sizeof(file)) {
                            strcat(file, ".gzi");
                        }
                    }

                    if (!relative_path_is_safe(file)) {
                        yk_warn("%s: [%s Entry] icon path \"%s\" must be relative to resources/ - ignoring.", bundle, t->ext, file);
                    } else {
                        snprintf(t->icon_file, sizeof(t->icon_file), "%s/resources/%s", bundle, file);
                        snprintf(t->icon_tag, sizeof(t->icon_tag), "%s", tag);
                    }
                }
            } else if (strcasecmp(n->key, "handler") == 0) {
                if (valid_token(n->values[0]))
                    snprintf(t->handler, sizeof(t->handler), "%s", n->values[0]);
                else
                    yk_warn("%s: handler invalid - using \"open\".", bundle);
            } else {
                if (t->prop_count < YK_MAX_PROPS) {
                    ykProperty *pr = &t->props[t->prop_count++];
                    snprintf(pr->key, sizeof(pr->key), "%s", n->key);
                    join_values(n, pr->val, sizeof(pr->val));
                }
            }
            n = n->next;
        }

        sec = sec->next;
    }

    free_entry_document(doc);

    if (s.ntypes == 0)
        return 0;

    if (!s.exec_rel[0] || !relative_path_is_safe(s.exec_rel)) {
        return 0;
    }

    char exe[PATH_MAX_LEN];
    int n = snprintf(exe, sizeof(exe), "%s/%s", bundle, s.exec_rel);
    struct stat st;

    if (n < 0 || (size_t)n >= sizeof(exe) ||
        stat(exe, &st) != 0 || !S_ISREG(st.st_mode) ||
        access(exe, X_OK) != 0) {
        return 0;
    }

    for (int i = 0; i < s.ntypes; i++)
        snprintf(types[i].exec_path, sizeof(types[i].exec_path), "%s", exe);

    return s.ntypes;
}

typedef struct {
    const char *want_ext;
    ykFileType *out;
    int         max;
    int         count;
    int         found;
} FtCollect;

static int ft_bundle_visit(const char *bundle_path, void *user) {
    FtCollect  *c = user;
    ykFileType *types = malloc(sizeof(ykFileType) * BUNDLE_MAX_TYPES);
    int         keep_going = 1;

    if (!types)
        return 0;

    int n = bundle_filetypes(bundle_path, types);

    for (int i = 0; i < n && keep_going; i++) {
        if (c->want_ext) {
            if (strcmp(types[i].ext, c->want_ext) == 0) {
                *c->out = types[i];
                c->found = 1;
                keep_going = 0;
            }
            continue;
        }

        int dup = 0;
        for (int j = 0; j < c->count; j++)
            if (strcmp(c->out[j].ext, types[i].ext) == 0)
                dup = 1;

        if (!dup && c->count < c->max)
            c->out[c->count++] = types[i];

        if (c->count >= c->max)
            keep_going = 0;
    }

    free(types);
    return keep_going;
}

static const char *apps_path_value(void) {
    const char *env = getenv(YK_APPS_PATH_ENV);
    return (env && *env) ? env : YK_APPS_PATH_DEFAULT;
}

int yk_file_extension(const char *path, char *out, size_t out_size) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;

    const char *dot = strrchr(base, '.');
    if (!dot || dot == base || dot[1] == '\0')
        return 1;

    return normalize_ext(dot + 1, out, out_size) ? 0 : 1;
}

int yk_find_filetype(const char *ext, ykFileType *out) {
    char norm[YK_EXT_MAX];

    if (!normalize_ext(ext, norm, sizeof(norm)))
        return 2;

    FtCollect c;
    memset(&c, 0, sizeof(c));
    c.want_ext = norm;
    c.out      = out;

    scan_path_list(apps_path_value(), ft_bundle_visit, &c);

    return c.found ? 0 : 1;
}

int yk_list_filetypes(ykFileType list[], int max, int *count) {
    FtCollect c;
    memset(&c, 0, sizeof(c));
    c.out = list;
    c.max = max;

    if (max > 0)
        scan_path_list(apps_path_value(), ft_bundle_visit, &c);

    *count = c.count;
    return 0;
}


// ------------------------------------------------
// Current bundle detection
// ------------------------------------------------

static char *argv0_ptr = NULL;

void yk_set_argv0(char *argv0) {
    argv0_ptr = argv0;
}

const char* yk_get_current_bundle_path() {
    static char path[PATH_MAX_LEN];

    if (!argv0_ptr) return NULL;
    if (!realpath(argv0_ptr, path))
        return NULL;

    char *p = strrchr(path, '/');
    if (p) *p = '\0';

    return path;
}

const char* yk_get_current_bundle() {
    const char *bundle_path = yk_get_current_bundle_path();
    if (!bundle_path) return NULL;

    static char bundle[NAME_MAX_LEN];
    const char *p = strrchr(bundle_path, '/');
    if (!p) return NULL;

    strncpy(bundle, p + 1, NAME_MAX_LEN);
    bundle[NAME_MAX_LEN-1] = '\0';

    return bundle;
}

const char *yk_get_current_bundle_icon() {
    static char icon_path[PATH_MAX_LEN];
    const char *bundle_path = yk_get_current_bundle_path();

    if (!bundle_path)
        return NULL;

    char meta_path[PATH_MAX_LEN];
    snprintf(meta_path, PATH_MAX_LEN, "%s/app.metadata", bundle_path);

    EntryDocument *doc = parse_entry_file(meta_path);
    if (!doc) return NULL;

    const EntryNode *icon_node = find_app_node(doc, "Icon");

    if (!icon_node) {
        free_entry_document(doc);
        return NULL;
    }

    if (!build_icon_path_from_entry(bundle_path, icon_node, icon_path, sizeof(icon_path))) {
        free_entry_document(doc);
        return NULL;
    }

    free_entry_document(doc);
    return icon_path;
}
