/*
 * yk-open-file - open a file with the application registered for its type
 * (the YK desktop's answer to xdg-open).
 *
 * Uses libYekara's bundle and entry parser systems.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

#include "yk/bundle.h"
#include "yk/entry.h"

#define PROG                "yk-open-file"

#define MAX_PROPS           16

#define EXIT_USAGE_ERR      1
#define EXIT_FILE_ERR       2
#define EXIT_NO_HANDLER     3
#define EXIT_LAUNCH_ERR     4

typedef struct {
    char key[64];
    char val[256];
} Prop;

typedef struct {
    char bundle[PATH_MAX];
    char exe[PATH_MAX * 2];
    char handler[64];
    Prop props[MAX_PROPS];
    int  nprops;
} Handler;

static int verbose = 0;

static void fail(int code, const char *fmt, ...)
    __attribute__((noreturn, format(printf, 2, 3)));

static void fail(int code, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, PROG ": ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(code);
}

static void warn(const char *fmt, ...)
    __attribute__((format(printf, 1, 2)));

static void warn(const char *fmt, ...)
{
    va_list ap;
    if (!verbose)
        return;
    fprintf(stderr, PROG ": warning: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* Letters, digits, '_' and '-' only (non-empty). */
static int valid_token(const char *s)
{
    if (!*s)
        return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-')
            return 0;
    return 1;
}

/* Helper to strip trailing " Entry" from section headers */
static void get_base_section_name(const char *raw_name, char *out, size_t out_len) {
    snprintf(out, out_len, "%s", raw_name);
    size_t len = strlen(out);
    if (len > 6 && strcasecmp(out + len - 6, " Entry") == 0) {
        out[len - 6] = '\0';
    }
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
 * Load handler details for a given file extension from a bundle using entry.h
 */
static int load_handler_from_bundle(const char *bundle_path, const char *ext, Handler *h)
{
    char meta_path[PATH_MAX * 2];
    snprintf(meta_path, PATH_MAX * 2, "%s/app.metadata", bundle_path);

    EntryDocument *doc = parse_entry_file(meta_path);
    if (!doc)
        return 0;

    char exec_rel[PATH_MAX] = "";
    int matching_found = 0;

    EntrySection *sec = doc->sections;
    while (sec) {
        char base_sec[64];
        get_base_section_name(sec->name, base_sec, sizeof(base_sec));

        if (strcasecmp(base_sec, "Application") == 0) {
            EntryNode *n = sec->entries;
            while (n) {
                if (strcasecmp(n->key, "ExecFile") == 0) {
                    snprintf(exec_rel, sizeof(exec_rel), "%s", n->values[0]);
                }
                n = n->next;
            }
        }
        sec = sec->next;
    }

    if (!exec_rel[0]) {
        free_entry_document(doc);
        return 0;
    }

    char exe[PATH_MAX * 2];
    snprintf(exe, sizeof(exe), "%s/%s", bundle_path, exec_rel);
    struct stat st;
    if (stat(exe, &st) != 0 || !S_ISREG(st.st_mode) || access(exe, X_OK) != 0) {
        free_entry_document(doc);
        return 0;
    }

    // Now look for the extension section (e.g., [txt Entry] -> base_sec "txt")
    sec = doc->sections;
    while (sec) {
        char base_sec[64];
        get_base_section_name(sec->name, base_sec, sizeof(base_sec));

        if (strcasecmp(base_sec, ext) == 0) {
            memset(h, 0, sizeof(*h));
            snprintf(h->bundle, sizeof(h->bundle), "%s", bundle_path);
            snprintf(h->exe, sizeof(h->exe), "%s", exe);
            snprintf(h->handler, sizeof(h->handler), "open");
            matching_found = 1;

            EntryNode *n = sec->entries;
            while (n) {
                if (strcasecmp(n->key, "icon") == 0) {
                    // Ignored here (file manager concern)
                } else if (strcasecmp(n->key, "handler") == 0) {
                    if (valid_token(n->values[0]))
                        snprintf(h->handler, sizeof(h->handler), "%s", n->values[0]);
                    else
                        warn("%s: [%s Entry] handler=\"%s\" must be a valid token - using \"open\".", bundle_path, ext, n->values[0]);
                } else if (!valid_token(n->key)) {
                    warn("%s: [%s Entry] property name \"%s\" is invalid - ignoring.", bundle_path, ext, n->key);
                } else if (h->nprops >= MAX_PROPS) {
                    warn("%s: [%s Entry] has too many properties - ignoring \"%s\".", bundle_path, ext, n->key);
                } else {
                    Prop *pr = &h->props[h->nprops++];
                    snprintf(pr->key, sizeof(pr->key), "%s", n->key);
                    join_values(n, pr->val, sizeof(pr->val));
                }
                n = n->next;
            }
            break;
        }
        sec = sec->next;
    }

    free_entry_document(doc);
    return matching_found;
}

/* Uses bundle.h to scan YK_APPS_PATH and locate the correct handler */
static int find_handler(const char *ext, Handler *h)
{
    ykFileType ft;
    if (yk_find_filetype(ext, &ft) == 0) {
        // Found via bundle.h filetype index; load full handler config details
        return load_handler_from_bundle(ft.bundle_path, ext, h);
    }
    return 0;
}

static int spawn_detached(const char *bundle, char *const argv[], char *err, size_t err_n)
{
    int pfd[2];
    if (pipe(pfd) != 0) {
        snprintf(err, err_n, "could not launch: %s", strerror(errno));
        return -1;
    }
    fcntl(pfd[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err, err_n, "could not launch: %s", strerror(errno));
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pfd[0]);
        setsid();

        if (chdir(bundle) != 0) {
            int e = errno;
            ssize_t w = write(pfd[1], &e, sizeof(e));
            (void)w;
            _exit(127);
        }

        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
            if (fd > 2) close(fd);
        }

        extern char **environ;
        execve(argv[0], argv, environ);

        int e = errno;
        ssize_t w = write(pfd[1], &e, sizeof(e));
        (void)w;
        _exit(127);
    }

    close(pfd[1]);

    int child_errno = 0;
    ssize_t r;
    do {
        r = read(pfd[0], &child_errno, sizeof(child_errno));
    } while (r < 0 && errno == EINTR);

    close(pfd[0]);

    if (r == (ssize_t)sizeof(child_errno)) {
        snprintf(err, err_n, "could not start application: %s", strerror(child_errno));
        return -1;
    }

    return 0;
}

static void print_quoted(const char *s)
{
    int plain = (*s != '\0');
    for (const char *p = s; *p; p++)
        if (!isalnum((unsigned char)*p) && !strchr("_./=:@%+-", *p))
            plain = 0;

    if (plain) {
        fputs(s, stdout);
        return;
    }

    putchar('\'');
    for (const char *p = s; *p; p++) {
        if (*p == '\'')
            fputs("'\\''", stdout);
        else
            putchar(*p);
    }
    putchar('\'');
}

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: " PROG " [options] [--] <file>\n\n"
        "Options:\n"
        "  -n, --dry-run   print the command instead of running it\n"
        "  -v, --verbose   warn about malformed metadata entries\n"
        "  -h, --help      show this help\n");
}

int main(int argc, char **argv)
{
    int dry_run = 0;
    const char *file_arg = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) {
            i++;
            break;
        } else if (!strcmp(a, "-n") || !strcmp(a, "--dry-run")) {
            dry_run = 1;
        } else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
            verbose = 1;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage(stdout);
            return 0;
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, PROG ": unknown option \"%s\"\n\n", a);
            usage(stderr);
            return EXIT_USAGE_ERR;
        } else {
            break;
        }
    }

    if (i >= argc) {
        usage(stderr);
        return EXIT_USAGE_ERR;
    }

    file_arg = argv[i++];

    char file[PATH_MAX];
    struct stat st;

    if (!realpath(file_arg, file))
        fail(EXIT_FILE_ERR, "cannot access \"%s\": %s", file_arg, strerror(errno));

    if (stat(file, &st) != 0)
        fail(EXIT_FILE_ERR, "cannot access \"%s\": %s", file, strerror(errno));

    if (S_ISDIR(st.st_mode))
        fail(EXIT_FILE_ERR, "\"%s\" is a directory, not a file", file);

    const char *base = strrchr(file, '/');
    base = base ? base + 1 : file;

    const char *dot = strrchr(base, '.');
    char ext[32];

    if (!dot || dot == base || yk_file_extension(file, ext, sizeof(ext)) != 0)
        fail(EXIT_NO_HANDLER, "\"%s\" has no file extension to look up", base);

    Handler h;
    if (!find_handler(ext, &h))
        fail(EXIT_NO_HANDLER, "no application is registered for .%s files", ext);

    char handler_arg[80];
    char prop_args[MAX_PROPS][352];
    char *cmd[MAX_PROPS + 4];
    int ac = 0;

    snprintf(handler_arg, sizeof(handler_arg), "--%s", h.handler);
    cmd[ac++] = h.exe;
    cmd[ac++] = handler_arg;
    cmd[ac++] = file;

    for (int k = 0; k < h.nprops; k++) {
        snprintf(prop_args[k], sizeof(prop_args[k]), "--%s=%s", h.props[k].key, h.props[k].val);
        cmd[ac++] = prop_args[k];
    }
    cmd[ac] = NULL;

    if (dry_run) {
        for (int k = 0; k < ac; k++) {
            if (k) putchar(' ');
            print_quoted(cmd[k]);
        }
        putchar('\n');
        return 0;
    }

    char err[256];
    if (spawn_detached(h.bundle, cmd, err, sizeof(err)) != 0)
        fail(EXIT_LAUNCH_ERR, "%s", err);

    return 0;
}
