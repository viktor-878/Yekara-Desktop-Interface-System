#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fsops.h"
#include "deskapp.h"
#include "state.h"
#include "util.h"

static int compare_items(const void *a, const void *b)
{
    const FileItem *x = a, *y = b;
    if (x->is_directory != y->is_directory)
        return x->is_directory ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

int scan_directory(const char *path, FileItem **out_list)
{
    *out_list = NULL;

    DIR *dir = opendir(path);
    if (!dir)
        return 0;

    int cap = 256;
    int count = 0;
    FileItem *list = malloc(cap * sizeof(FileItem));

    if (!list) {
        closedir(dir);
        return 0;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        const char *nm = entry->d_name;

        if (!strcmp(nm, ".") || !strcmp(nm, ".."))
            continue;
        if (!show_hidden && nm[0] == '.')
            continue;

        if (count == cap) {
            FileItem *tmp = realloc(list, (size_t)cap * 2 * sizeof(FileItem));
            if (!tmp)
                break;
            list = tmp;
            cap *= 2;
        }

        char full[PATH_MAX * 2];
        struct stat st;
        join_path(full, sizeof(full), path, nm);

        if (stat(full, &st) != 0 && lstat(full, &st) != 0)
            memset(&st, 0, sizeof(st));

        FileItem *f = &list[count];
        snprintf(f->name, sizeof(f->name), "%s", nm);
        f->is_app = (S_ISDIR(st.st_mode) && has_deskapp_suffix(nm) &&
                     is_deskapp_bundle(full)) ? True : False;
        f->is_directory = (S_ISDIR(st.st_mode) && !f->is_app) ? True : False;
        f->size  = st.st_size;
        f->mtime = st.st_mtime;
        count++;
    }

    closedir(dir);
    qsort(list, count, sizeof(FileItem), compare_items);

    *out_list = list;
    return count;
}

static int copy_file_contents(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in)
        return -1;

    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }

    char buf[8192];
    size_t n;
    int rc = 0;

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            rc = -1;
            break;
        }
    }

    fclose(in);
    if (fclose(out) != 0)
        rc = -1;

    struct stat st;
    if (stat(src, &st) == 0)
        chmod(dst, st.st_mode & 07777);

    return rc;
}

int copy_recursive(const char *src, const char *dst)
{
    struct stat st;

    if (lstat(src, &st) != 0)
        return -1;

    if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX];
        ssize_t len = readlink(src, target, sizeof(target) - 1);
        if (len < 0)
            return -1;
        target[len] = '\0';
        return symlink(target, dst);
    }

    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst, st.st_mode & 07777) != 0 && errno != EEXIST)
            return -1;

        DIR *dir = opendir(src);
        if (!dir)
            return -1;

        struct dirent *entry;
        int rc = 0;

        while ((entry = readdir(dir)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;

            char child_src[PATH_MAX * 2];
            char child_dst[PATH_MAX * 2];
            join_path(child_src, sizeof(child_src), src, entry->d_name);
            join_path(child_dst, sizeof(child_dst), dst, entry->d_name);

            if (copy_recursive(child_src, child_dst) != 0)
                rc = -1;
        }

        closedir(dir);
        return rc;
    }

    return copy_file_contents(src, dst);
}

int remove_recursive(const char *path)
{
    struct stat st;

    if (lstat(path, &st) != 0)
        return -1;

    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir)
            return -1;

        struct dirent *entry;
        int rc = 0;

        while ((entry = readdir(dir)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;

            char child[PATH_MAX * 2];
            join_path(child, sizeof(child), path, entry->d_name);

            if (remove_recursive(child) != 0)
                rc = -1;
        }

        closedir(dir);

        if (rmdir(path) != 0)
            rc = -1;

        return rc;
    }

    return (unlink(path) == 0) ? 0 : -1;
}
