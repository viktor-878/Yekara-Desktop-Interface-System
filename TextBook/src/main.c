#include <Xm/Xm.h>
#include <Xm/RowColumn.h>
#include <yk/window.h>
#include <Xm/Text.h>
#include <Xm/PushB.h>
#include <Xm/FileSB.h>
#include <Xm/Form.h>
#include <Xm/ScrolledW.h>
#include <Xm/CascadeB.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/xpm.h>
#include <yk/gzi.h>
#include <yk/style.h>
#include <yk/bundle.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <limits.h>

Widget text_w;

/* ---------------- File Helpers ---------------- */

/* Load a file into the text widget. Returns 0 on success, -1 on failure. */
static int load_file_into_text(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror(filename);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size < 0) {
        fprintf(stderr, "%s: cannot determine file size\n", filename);
        fclose(fp);
        return -1;
    }

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(fp);
        return -1;
    }

    size_t n = fread(buffer, 1, size, fp);
    buffer[n] = '\0';

    XmTextSetString(text_w, buffer);
    free(buffer);
    fclose(fp);
    return 0;
}

/* ---------------- File Menu Callbacks ---------------- */
void save_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    XmFileSelectionBoxCallbackStruct *cbs = (XmFileSelectionBoxCallbackStruct *)call_data;

    if (cbs->reason == XmCR_OK) {
        char *filename;
        if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &filename)) {

            FILE *fp = fopen(filename, "w");
            if (fp) {
                char *content = XmTextGetString(text_w);
                fwrite(content, 1, strlen(content), fp);
                XtFree(content);
                fclose(fp);
            }

            XtFree(filename);
        }
        XtUnmanageChild(w);
    }
}

void open_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    XmFileSelectionBoxCallbackStruct *cbs = (XmFileSelectionBoxCallbackStruct *)call_data;

    if (cbs->reason == XmCR_OK) {
        char *filename;
        if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &filename)) {
            load_file_into_text(filename);
            XtFree(filename);
        }
        XtUnmanageChild(w);
    }
}

void create_file_dialog(Widget parent, int is_save) {
    Widget dialog = XmCreateFileSelectionDialog(parent, is_save ? "Save As" : "Open", NULL, 0);

    if (is_save)
        XtAddCallback(dialog, XmNokCallback, save_callback, NULL);
    else
        XtAddCallback(dialog, XmNokCallback, open_callback, NULL);

    XtAddCallback(dialog, XmNcancelCallback, (XtCallbackProc)XtUnmanageChild, NULL);
    XtManageChild(dialog);
}

void file_menu_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    int action = (intptr_t)client_data;

    switch (action) {
        case 0: create_file_dialog(XtParent(w), 0); break;
        case 1: create_file_dialog(XtParent(w), 1); break;
        case 2: exit(0); break;
    }
}

void edit_menu_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    int action = (intptr_t)client_data;

    switch (action) {
        case 0: XmTextCut(text_w, CurrentTime); break;
        case 1: XmTextCopy(text_w, CurrentTime); break;
        case 2: XmTextPaste(text_w); break;
        case 3: XmTextSetSelection(text_w, 0, XmTextGetLastPosition(text_w), CurrentTime); break;
    }
}



/* ---------------- Main ---------------- */
int main(int argc, char **argv) {
    XtAppContext app;
    Widget toplevel, form, menubar;
    Widget file_menu, edit_menu, file_cascade, edit_cascade, scrolled_w;
    Widget open_item, save_item, quit_item;
    Widget cut_item, copy_item, paste_item, select_all_item;
    const char *open_path = NULL;

    /* Parse and strip --open FILE / --open=FILE before the toolkit sees argv */
    for (int i = 1; i < argc; i++) {
        int consumed = 0;

        if (strcmp(argv[i], "--open") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Usage: %s [--open FILE]\n", argv[0]);
                return 1;
            }
            open_path = argv[i + 1];
            consumed = 2;
        } else if (strncmp(argv[i], "--open=", 7) == 0) {
            open_path = argv[i] + 7;
            consumed = 1;
        }

        if (consumed) {
            /* shift remaining args down over the consumed ones
               (copies the trailing NULL too, keeping argv terminated) */
            for (int j = i; j + consumed <= argc; j++)
                argv[j] = argv[j + consumed];
            argc -= consumed;
            break;
        }
    }

    toplevel = YkInitWindow(&app, "XmTextEditor", &argc, argv);
    XtVaSetValues(toplevel, XmNwidth, 720, XmNheight, 480, NULL);

    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, toplevel, NULL);

    menubar = XmCreateMenuBar(form, "menubar", NULL, 0);
    XtVaSetValues(menubar,
                  XmNtopAttachment, XmATTACH_FORM,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM, NULL);
    XtManageChild(menubar);

    /* File Menu */
    file_menu = XmCreatePulldownMenu(menubar, "fileMenu", NULL, 0);

    open_item = XmCreatePushButton(file_menu, "Open", NULL, 0);
    XtAddCallback(open_item, XmNactivateCallback, file_menu_callback, (XtPointer)0);
    XtManageChild(open_item);

    save_item = XmCreatePushButton(file_menu, "Save", NULL, 0);
    XtAddCallback(save_item, XmNactivateCallback, file_menu_callback, (XtPointer)1);
    XtManageChild(save_item);

    quit_item = XmCreatePushButton(file_menu, "Quit", NULL, 0);
    XtAddCallback(quit_item, XmNactivateCallback, file_menu_callback, (XtPointer)2);
    XtManageChild(quit_item);

    file_cascade = XtVaCreateManagedWidget("File", xmCascadeButtonWidgetClass, menubar,
                                           XmNsubMenuId, file_menu, NULL);

    /* Edit Menu */
    edit_menu = XmCreatePulldownMenu(menubar, "editMenu", NULL, 0);

    cut_item = XmCreatePushButton(edit_menu, "Cut", NULL, 0);
    XtAddCallback(cut_item, XmNactivateCallback, edit_menu_callback, (XtPointer)0);
    XtManageChild(cut_item);

    copy_item = XmCreatePushButton(edit_menu, "Copy", NULL, 0);
    XtAddCallback(copy_item, XmNactivateCallback, edit_menu_callback, (XtPointer)1);
    XtManageChild(copy_item);

    paste_item = XmCreatePushButton(edit_menu, "Paste", NULL, 0);
    XtAddCallback(paste_item, XmNactivateCallback, edit_menu_callback, (XtPointer)2);
    XtManageChild(paste_item);

    select_all_item = XmCreatePushButton(edit_menu, "Select All", NULL, 0);
    XtAddCallback(select_all_item, XmNactivateCallback, edit_menu_callback, (XtPointer)3);
    XtManageChild(select_all_item);

    edit_cascade = XtVaCreateManagedWidget("Edit", xmCascadeButtonWidgetClass, menubar,
                                           XmNsubMenuId, edit_menu, NULL);

    /* Text Area */
    scrolled_w = XmCreateScrolledWindow(form, "scrolled_w", NULL, 0);
    XtVaSetValues(scrolled_w,
                  XmNtopAttachment, XmATTACH_WIDGET,
                  XmNtopWidget, menubar,
                  XmNleftAttachment, XmATTACH_FORM,
                  XmNrightAttachment, XmATTACH_FORM,
                  XmNbottomAttachment, XmATTACH_FORM,
                  XmNscrollBarDisplayPolicy, XmSTATIC,
                  NULL);

    text_w = XmCreateText(scrolled_w, "text_w", NULL, 0);
    XtVaSetValues(text_w,
                  XmNeditMode, XmMULTI_LINE_EDIT,
                  XmNbackground, WhitePixelOfScreen(XtScreen(toplevel)),
                  XmNuserData, YK_SKIP_THEME_FLAG,
                  NULL);

    XtManageChild(text_w);
    XtManageChild(scrolled_w);

    /* Load the file given with --open, if any */
    if (open_path)
        load_file_into_text(open_path);

    /* Accelerators */
    XtVaSetValues(open_item, XmNaccelerator, "Ctrl<Key>O", XmNacceleratorText, XmStringCreateLocalized("Ctrl+O"), NULL);
    XtVaSetValues(save_item, XmNaccelerator, "Ctrl<Key>S", XmNacceleratorText, XmStringCreateLocalized("Ctrl+S"), NULL);
    XtVaSetValues(quit_item, XmNaccelerator, "Ctrl<Key>Q", XmNacceleratorText, XmStringCreateLocalized("Ctrl+Q"), NULL);
    XtVaSetValues(cut_item, XmNaccelerator, "Ctrl<Key>X", XmNacceleratorText, XmStringCreateLocalized("Ctrl+X"), NULL);
    XtVaSetValues(copy_item, XmNaccelerator, "Ctrl<Key>C", XmNacceleratorText, XmStringCreateLocalized("Ctrl+C"), NULL);
    XtVaSetValues(paste_item, XmNaccelerator, "Ctrl<Key>V", XmNacceleratorText, XmStringCreateLocalized("Ctrl+V"), NULL);
    XtVaSetValues(select_all_item, XmNaccelerator, "Ctrl<Key>A", XmNacceleratorText, XmStringCreateLocalized("Ctrl+A"), NULL);

    XtRealizeWidget(toplevel);

    XtAppMainLoop(app);

    return 0;
}
