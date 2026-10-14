/*
 * .deskapp bundle handling, built on libYekara's bundle and entry parsers.
 */

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/stat.h>

#include <yk/bundle.h>
#include <yk/entry.h>

#include "deskapp.h"
#include "util.h"

Boolean has_deskapp_suffix(const char *name)
{
    size_t n = strlen(name);
    size_t s = strlen(DESKAPP_SUFFIX);
    return (n > s && strcasecmp(name + n - s, DESKAPP_SUFFIX) == 0) ? True : False;
}

Boolean is_deskapp_bundle(const char *path)
{
    struct stat st;
    char meta[PATH_MAX * 2];

    if (!has_deskapp_suffix(path))
        return False;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
        return False;

    join_path(meta, sizeof(meta), path, DESKAPP_METADATA);
    return (stat(meta, &st) == 0 && S_ISREG(st.st_mode)) ? True : False;
}

int deskapp_read_exec(const char *bundle, char *out, size_t out_n)
{
    return (yk_get_property(bundle, "ExecFile", out, out_n) == 0) ? 0 : -1;
}

int deskapp_read_icon(const char *bundle,
                      char *out_file, size_t out_file_n,
                      char *out_tag,  size_t out_tag_n)
{
    char meta[PATH_MAX * 2];
    join_path(meta, sizeof(meta), bundle, DESKAPP_METADATA);

    EntryDocument *doc = parse_entry_file(meta);
    if (!doc)
        return -1;

    const EntryMultiValue *multi = NULL;
    EntrySection *sec = doc->sections;
    while (sec) {
        if (strcasecmp(sec->name, "Application") == 0 ||
            strcasecmp(sec->name, "Application Entry") == 0) {
            EntryNode *n = sec->entries;
            while (n) {
                if (strcasecmp(n->key, "Icon") == 0) {
                    if (n->is_multi)
                        multi = &n->multi;
                    break;
                }
                n = n->next;
            }
        }
        sec = sec->next;
    }

    if (!multi || !multi->value || !multi->tag) {
        free_entry_document(doc);
        return -1;
    }

    snprintf(out_file, out_file_n, "%s", multi->value);
    if (!ends_with(out_file, ".gzi"))
        strcat(out_file, ".gzi");

    snprintf(out_tag, out_tag_n, "%s", multi->tag);
    free_entry_document(doc);
    return 0;
}

Boolean bundle_relative_path_is_safe(const char *rel)
{
    if (rel[0] == '/')
        return False;

    const char *p = rel;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);

        if (len == 2 && p[0] == '.' && p[1] == '.')
            return False;
        if (!slash)
            break;
        p = slash + 1;
    }
    return True;
}
