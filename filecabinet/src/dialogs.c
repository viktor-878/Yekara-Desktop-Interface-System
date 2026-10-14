#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Xm/Xm.h>
#include <Xm/SelectioB.h>
#include <Xm/MessageB.h>

#include "dialogs.h"
#include "deskapp.h"
#include "fsops.h"
#include "state.h"
#include "util.h"
#include "view.h"

static void destroy_dialog(Widget box)
{
    Widget shell = XtParent(box);
    XtDestroyWidget(XtIsShell(shell) ? shell : box);
}

static void generic_dialog_destroy_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    destroy_dialog(w);
}

/* ---- Error ------------------------------------------------------------- */

void show_error_dialog(Widget parent, const char *message)
{
    Arg args[1];
    XmString msgStr = XmStringCreateLocalized((char *)message);

    XtSetArg(args[0], XmNmessageString, msgStr);

    Widget dialog = XmCreateErrorDialog(toplevel_widget, "errorDialog", args, 1);
    XmStringFree(msgStr);

    XtUnmanageChild(XmMessageBoxGetChild(dialog, XmDIALOG_CANCEL_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(dialog, XmDIALOG_HELP_BUTTON));

    XtAddCallback(dialog, XmNokCallback, generic_dialog_destroy_cb, NULL);
    XtManageChild(dialog);
}

/* ---- Rename ------------------------------------------------------------ */

static void rename_dialog_ok_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    XmSelectionBoxCallbackStruct *cbs = (XmSelectionBoxCallbackStruct *)call_data;
    char *new_name = NULL;

    if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &new_name) &&
        new_name && strlen(new_name) > 0) {

        char old_path[PATH_MAX * 2];
        char new_path[PATH_MAX * 2];

        join_path(old_path, sizeof(old_path), current_path, selected_node_name);
        join_path(new_path, sizeof(new_path), current_path, new_name);

        if (strchr(new_name, '/') || !strcmp(new_name, ".") || !strcmp(new_name, "..")) {
            show_error_dialog(w, "Invalid name.");
        } else if (strcmp(new_name, selected_node_name) != 0 && path_exists(new_path)) {
            show_error_dialog(w, "An item with that name already exists.");
        } else if (rename(old_path, new_path) != 0) {
            show_error_dialog(w, "Could not rename item.");
        }

        render_view();
    }

    if (new_name)
        XtFree(new_name);

    destroy_dialog(w);
}

void show_rename_dialog(Widget parent)
{
    if (strlen(selected_node_name) == 0 || strcmp(selected_node_name, "..") == 0)
        return;

    Arg args[2];
    int n = 0;

    XmString title = XmStringCreateLocalized("Rename Item");
    XtSetArg(args[n], XmNdialogTitle, title); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;

    Widget dialog = XmCreatePromptDialog(toplevel_widget, "renameDialog", args, n);
    XmStringFree(title);

    XtUnmanageChild(XmSelectionBoxGetChild(dialog, XmDIALOG_HELP_BUTTON));

    XmString defText = XmStringCreateLocalized(selected_node_name);
    XtVaSetValues(dialog, XmNtextString, defText, NULL);
    XmStringFree(defText);

    XtAddCallback(dialog, XmNokCallback, rename_dialog_ok_cb, NULL);
    XtAddCallback(dialog, XmNcancelCallback, generic_dialog_destroy_cb, NULL);
    XtManageChild(dialog);
}

/* ---- New folder -------------------------------------------------------- */

static void new_folder_dialog_ok_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    XmSelectionBoxCallbackStruct *cbs = (XmSelectionBoxCallbackStruct *)call_data;
    char *folder_name = NULL;

    if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &folder_name) &&
        folder_name && strlen(folder_name) > 0) {

        char new_path[PATH_MAX * 2];
        join_path(new_path, sizeof(new_path), current_path, folder_name);

        if (strchr(folder_name, '/') || !strcmp(folder_name, ".") || !strcmp(folder_name, "..")) {
            show_error_dialog(w, "Invalid folder name.");
        } else if (mkdir(new_path, 0755) != 0) {
            show_error_dialog(w, "Could not create folder.");
        }

        render_view();
    }

    if (folder_name)
        XtFree(folder_name);

    destroy_dialog(w);
}

void show_new_folder_dialog(Widget parent)
{
    Arg args[2];
    int n = 0;

    XmString title = XmStringCreateLocalized("New Folder");
    XtSetArg(args[n], XmNdialogTitle, title); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;

    Widget dialog = XmCreatePromptDialog(toplevel_widget, "newFolderDialog", args, n);
    XmStringFree(title);

    XtUnmanageChild(XmSelectionBoxGetChild(dialog, XmDIALOG_HELP_BUTTON));

    XmString defText = XmStringCreateLocalized("New Folder");
    XtVaSetValues(dialog, XmNtextString, defText, NULL);
    XmStringFree(defText);

    XtAddCallback(dialog, XmNokCallback, new_folder_dialog_ok_cb, NULL);
    XtAddCallback(dialog, XmNcancelCallback, generic_dialog_destroy_cb, NULL);
    XtManageChild(dialog);
}

/* ---- Delete ------------------------------------------------------------ */

static void delete_confirm_ok_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    char target[PATH_MAX * 2];
    join_path(target, sizeof(target), current_path, selected_node_name);

    if (remove_recursive(target) != 0)
        show_error_dialog(w, "Could not delete item.");

    selected_node_name[0] = '\0';
    render_view();
    destroy_dialog(w);
}

void show_delete_confirm_dialog(Widget parent)
{
    if (strlen(selected_node_name) == 0 || strcmp(selected_node_name, "..") == 0)
        return;

    char message[512];
    snprintf(message, sizeof(message), "Permanently delete \"%s\"?", selected_node_name);

    Arg args[2];
    int n = 0;

    XmString msgStr = XmStringCreateLocalized(message);
    XtSetArg(args[n], XmNmessageString, msgStr); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;

    Widget dialog = XmCreateQuestionDialog(toplevel_widget, "deleteConfirmDialog", args, n);
    XmStringFree(msgStr);

    XtUnmanageChild(XmMessageBoxGetChild(dialog, XmDIALOG_HELP_BUTTON));

    XtAddCallback(dialog, XmNokCallback, delete_confirm_ok_cb, NULL);
    XtAddCallback(dialog, XmNcancelCallback, generic_dialog_destroy_cb, NULL);
    XtManageChild(dialog);
}

/* ---- Properties -------------------------------------------------------- */

void show_properties_dialog(Widget parent)
{
    if (strlen(selected_node_name) == 0 || strcmp(selected_node_name, "..") == 0)
        return;

    char full_path[PATH_MAX * 2];
    join_path(full_path, sizeof(full_path), current_path, selected_node_name);

    struct stat st;
    char info[1024];

    if (stat(full_path, &st) == 0) {
        char perms[11];
        perms[0]  = S_ISDIR(st.st_mode) ? 'd' : '-';
        perms[1]  = (st.st_mode & S_IRUSR) ? 'r' : '-';
        perms[2]  = (st.st_mode & S_IWUSR) ? 'w' : '-';
        perms[3]  = (st.st_mode & S_IXUSR) ? 'x' : '-';
        perms[4]  = (st.st_mode & S_IRGRP) ? 'r' : '-';
        perms[5]  = (st.st_mode & S_IWGRP) ? 'w' : '-';
        perms[6]  = (st.st_mode & S_IXGRP) ? 'x' : '-';
        perms[7]  = (st.st_mode & S_IROTH) ? 'r' : '-';
        perms[8]  = (st.st_mode & S_IWOTH) ? 'w' : '-';
        perms[9]  = (st.st_mode & S_IXOTH) ? 'x' : '-';
        perms[10] = '\0';

        char time_buf[64] = "unknown";
        struct tm *tm_info = localtime(&st.st_mtime);
        if (tm_info)
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

        snprintf(info, sizeof(info),
                 "Name:        %s\n"
                 "Type:        %s\n"
                 "Size:        %ld bytes\n"
                 "Permissions: %s\n"
                 "Modified:    %s",
                 selected_node_name,
                 is_deskapp_bundle(full_path) ? "Application (deskapp bundle)" :
                 S_ISDIR(st.st_mode) ? "Folder" : "File",
                 (long)st.st_size, perms, time_buf);
    } else {
        snprintf(info, sizeof(info), "Could not read properties for %s", selected_node_name);
    }

    Arg args[2];
    int n = 0;

    XmString infoStr = XmStringCreateLtoR(info, XmFONTLIST_DEFAULT_TAG);
    XtSetArg(args[n], XmNmessageString, infoStr); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;

    Widget dialog = XmCreateInformationDialog(toplevel_widget, "propertiesDialog", args, n);
    XmStringFree(infoStr);

    XtUnmanageChild(XmMessageBoxGetChild(dialog, XmDIALOG_CANCEL_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(dialog, XmDIALOG_HELP_BUTTON));

    XtAddCallback(dialog, XmNokCallback, generic_dialog_destroy_cb, NULL);
    XtManageChild(dialog);
}
