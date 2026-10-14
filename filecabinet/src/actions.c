#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>

#include <Xm/Xm.h>
#include <Xm/ToggleB.h>

#include "actions.h"
#include "deskapp.h"
#include "dialogs.h"
#include "fsops.h"
#include "nav.h"
#include "state.h"
#include "util.h"
#include "view.h"

void toggle_hidden_cb(Widget w, XtPointer c, XtPointer d)
{
    XmToggleButtonCallbackStruct *cbs = (XmToggleButtonCallbackStruct *)d;
    show_hidden = cbs->set ? True : False;
    render_view();
}

void context_menu_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    char *action = (char *)client_data;

    if (strlen(selected_node_name) == 0)
        return;

    if (strcmp(selected_node_name, "..") == 0 && strcmp(action, "open") != 0)
        return;

    if (strcmp(action, "open") == 0) {
        char nm[256];
        snprintf(nm, sizeof(nm), "%s", selected_node_name);
        activate_item(nm);
    } else if (strcmp(action, "contents") == 0) {
        char full[PATH_MAX * 2];
        join_path(full, sizeof(full), current_path, selected_node_name);
        if (is_deskapp_bundle(full)) {
            char nm[256];
            snprintf(nm, sizeof(nm), "%s", selected_node_name);
            change_directory(nm);
        }
    } else if (strcmp(action, "rename") == 0) {
        show_rename_dialog(w);
    } else if (strcmp(action, "move") == 0 || strcmp(action, "copy") == 0) {
        join_path(source_move_path, sizeof(source_move_path), current_path, selected_node_name);
        is_moving_file  = (strcmp(action, "move") == 0) ? True : False;
        is_copying_file = !is_moving_file;
        update_status_bar();
    } else if (strcmp(action, "delete") == 0) {
        show_delete_confirm_dialog(w);
    } else if (strcmp(action, "properties") == 0) {
        show_properties_dialog(w);
    }
}

void canvas_menu_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    if (!is_moving_file && !is_copying_file)
        return;

    const char *name = strrchr(source_move_path, '/');
    if (!name)
        return;
    name++;

    struct stat st;
    if (lstat(source_move_path, &st) != 0) {
        show_error_dialog(w, "The source item no longer exists.");
        is_moving_file = is_copying_file = False;
        source_move_path[0] = '\0';
        update_status_bar();
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        size_t l = strlen(source_move_path);
        if (strncmp(current_path, source_move_path, l) == 0 &&
            (current_path[l] == '/' || current_path[l] == '\0')) {
            show_error_dialog(w, "Cannot paste a folder into itself.");
            return;
        }
    }

    char dest[PATH_MAX * 2];

    if (is_moving_file) {
        join_path(dest, sizeof(dest), current_path, name);

        if (strcmp(dest, source_move_path) == 0) {
            is_moving_file = False;
            source_move_path[0] = '\0';
            update_status_bar();
            return;
        }

        if (path_exists(dest)) {
            show_error_dialog(w, "An item with that name already exists here.");
            return;
        }

        Boolean ok = (rename(source_move_path, dest) == 0);
        if (!ok && errno == EXDEV)
            ok = (copy_recursive(source_move_path, dest) == 0 &&
                  remove_recursive(source_move_path) == 0);

        if (ok) {
            is_moving_file = False;
            source_move_path[0] = '\0';
            render_view();
        } else {
            show_error_dialog(w, "Could not move item here.");
        }
    } else {
        make_unique_path(current_path, name, dest, sizeof(dest));

        if (copy_recursive(source_move_path, dest) == 0)
            render_view();
        else
            show_error_dialog(w, "Could not copy item here.");
    }
}

void new_folder_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    show_new_folder_dialog(w);
}

void refresh_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    render_view();
}

void exit_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    exit(0);
}

void icon_view_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    use_icon_view = True;
    render_view();
}

void list_view_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    use_icon_view = False;
    render_view();
}
