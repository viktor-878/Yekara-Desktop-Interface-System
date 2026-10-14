#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>

#include "nav.h"
#include "config.h"
#include "deskapp.h"
#include "dialogs.h"
#include "launch.h"
#include "state.h"
#include "util.h"
#include "view.h"

static char history[HISTORY_MAX][PATH_MAX];
static int  history_len = 0;
static int  history_pos = -1;

void history_push(const char *p)
{
    if (history_pos >= 0 && strcmp(history[history_pos], p) == 0)
        return;

    history_len = history_pos + 1;

    if (history_len == HISTORY_MAX) {
        memmove(history[0], history[1], (HISTORY_MAX - 1) * PATH_MAX);
        history_len--;
    }

    snprintf(history[history_len], PATH_MAX, "%s", p);
    history_pos = history_len;
    history_len++;
}

static Boolean navigate_to(const char *path, Boolean record)
{
    char resolved[PATH_MAX];

    if (!realpath(path, resolved)) {
        show_error_dialog(gridContainer, "That folder does not exist.");
        return False;
    }

    DIR *d = opendir(resolved);
    if (!d) {
        show_error_dialog(gridContainer, "Cannot open folder (permission denied?).");
        return False;
    }
    closedir(d);

    if (record)
        history_push(resolved);

    snprintf(current_path, sizeof(current_path), "%s", resolved);
    render_view();
    return True;
}

void change_directory(const char *target_node)
{
    char pending[PATH_MAX * 2];

    if (target_node[0] == '/') {
        snprintf(pending, sizeof(pending), "%s", target_node);
    } else if (strcmp(target_node, "..") == 0) {
        snprintf(pending, sizeof(pending), "%s", current_path);
        char *s = strrchr(pending, '/');
        if (s && s != pending) *s = '\0';
        else                   strcpy(pending, "/");
    } else {
        join_path(pending, sizeof(pending), current_path, target_node);
    }

    navigate_to(pending, True);
}

void activate_item(const char *name)
{
    if (strcmp(name, "..") == 0 || name[0] == '/') {
        change_directory(name);
        return;
    }

    char full[PATH_MAX * 2];
    struct stat st;
    join_path(full, sizeof(full), current_path, name);

    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (is_deskapp_bundle(full)) {
            char err[256];
            if (!launch_deskapp(full, err, sizeof(err)))
                show_error_dialog(gridContainer, err);
        } else {
            change_directory(name);
        }
    } else {
        open_with_default_app(full);
    }
}

void go_back_cb(Widget w, XtPointer c, XtPointer d)
{
    if (history_pos > 0) {
        history_pos--;
        if (!navigate_to(history[history_pos], False))
            history_pos++;
    }
}

void go_forward_cb(Widget w, XtPointer c, XtPointer d)
{
    if (history_pos + 1 < history_len) {
        history_pos++;
        if (!navigate_to(history[history_pos], False))
            history_pos--;
    }
}

void go_up_cb(Widget w, XtPointer c, XtPointer d)
{
    change_directory("..");
}

void go_home_cb(Widget w, XtPointer c, XtPointer d)
{
    const char *home = getenv("HOME");
    change_directory(home ? home : "/");
}
