#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "util.h"

void join_path(char *out, size_t n, const char *dir, const char *name)
{
    snprintf(out, n, "%s%s%s", dir,
             (dir[0] == '/' && dir[1] == '\0') ? "" : "/", name);
}

Boolean path_exists(const char *p)
{
    struct stat st;
    return lstat(p, &st) == 0;
}

int ends_with(const char *s, const char *suffix)
{
    size_t sl = strlen(s), su = strlen(suffix);
    return su <= sl && strcmp(s + sl - su, suffix) == 0;
}

void make_unique_path(const char *dir, const char *name, char *out, size_t n)
{
    join_path(out, n, dir, name);
    if (!path_exists(out))
        return;

    const char *dot = strrchr(name, '.');
    int base_len = (dot && dot != name) ? (int)(dot - name) : (int)strlen(name);
    const char *ext = (dot && dot != name) ? dot : "";

    for (int i = 1; i < 1000; i++) {
        char cand[512];
        if (i == 1)
            snprintf(cand, sizeof(cand), "%.*s (copy)%s", base_len, name, ext);
        else
            snprintf(cand, sizeof(cand), "%.*s (copy %d)%s", base_len, name, i, ext);
        join_path(out, n, dir, cand);
        if (!path_exists(out))
            return;
    }
}

void format_size(off_t sz, char *out, size_t n)
{
    static const char *units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = (double)sz;
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
    if (i == 0) snprintf(out, n, "%ld B", (long)sz);
    else        snprintf(out, n, "%.1f %s", v, units[i]);
}

void describe_item(const FileItem *f, char *size_out, size_t sn,
                   char *date_out, size_t dn)
{
    if (f->is_directory)  snprintf(size_out, sn, "--");
    else if (f->is_app)   snprintf(size_out, sn, "App");
    else                  format_size(f->size, size_out, sn);

    time_t t = f->mtime;
    struct tm *tm = localtime(&t);
    if (tm) strftime(date_out, dn, "%Y-%m-%d %H:%M", tm);
    else    snprintf(date_out, dn, "?");
}
