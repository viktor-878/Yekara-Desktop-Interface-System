#include <Xm/Xm.h>
#include <Xm/RowColumn.h>
#include <Xm/Text.h>
#include <Xm/PushB.h>
#include <Xm/FileSB.h>
#include <Xm/Form.h>
#include <Xm/ScrolledW.h>
#include <Xm/CascadeB.h>
#include <stdio.h>
#include <stdlib.h>
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
#include <locale.h>   // ✅ IMPORTANT


Widget text_w;

/* ---------------- File Menu Callbacks ---------------- */
void save_callback(Widget w, XtPointer client_data, XtPointer call_data) {
    XmFileSelectionBoxCallbackStruct *cbs = (XmFileSelectionBoxCallbackStruct *)call_data;

    if (cbs->reason == XmCR_OK) {
        char *filename;
        if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &filename)) {

            FILE *fp = fopen(filename, "w");
            if (fp) {
                char *content = XmTextGetString(text_w); // UTF-8 buffer
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

            FILE *fp = fopen(filename, "r");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long size = ftell(fp);
                fseek(fp, 0, SEEK_SET);

                char *buffer = malloc(size + 1);
                fread(buffer, 1, size, fp);
                buffer[size] = '\0';

                XmTextSetString(text_w, buffer); // UTF-8 safe
                free(buffer);
                fclose(fp);
            }

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

/* ⚠️ REMOVED: verify_callback blocking Ctrl combos (breaks UTF input) */

/* ---------------- Icon Loader ---------------- */
static int load_bundle_icon(Widget toplevel, char *argv0) {

    yk_set_argv0(argv0);

    GZI_Image *img = gzi_load(yk_get_current_bundle_icon(), "textbook_xpm");
    if (!img) {
        fprintf(stderr, "GZI icon load failed\n");
        return 0;
    }

    char **xpm = gzi_build_xpm(img);
    if (!xpm) {
        fprintf(stderr, "XPM generation failed\n");
        gzi_free(img);
        return 0;
    }

    Pixmap pixmap, mask;
    XpmAttributes attr;
    memset(&attr, 0, sizeof(attr));

    attr.visual = DefaultVisualOfScreen(XtScreen(toplevel));
    attr.colormap = DefaultColormapOfScreen(XtScreen(toplevel));
    attr.depth = DefaultDepthOfScreen(XtScreen(toplevel));
    attr.valuemask = XpmVisual | XpmColormap | XpmDepth;

    int status = XpmCreatePixmapFromData(
        XtDisplay(toplevel),
        RootWindowOfScreen(XtScreen(toplevel)),
        xpm,
        &pixmap,
        &mask,
        &attr
    );

    if (status != XpmSuccess) {
        fprintf(stderr, "XPM build failed: %s\n", XpmGetErrorString(status));
        gzi_free(img);
        return 0;
    }

    XtVaSetValues(toplevel,
                  XmNiconPixmap, pixmap,
                  XmNiconMask, mask,
                  NULL);

    gzi_free(img);
    return 1;
}

/* ---------------- Main ---------------- */
int main(int argc, char **argv) {

    /* ✅ CRITICAL: enable UTF-8 */
    setlocale(LC_ALL, "");
    XtSetLanguageProc(NULL, NULL, NULL);

    XtAppContext app;
    Widget toplevel, form, menubar;
    Widget file_menu, edit_menu, file_cascade, edit_cascade, scrolled_w;
    Widget open_item, save_item, quit_item;
    Widget cut_item, copy_item, paste_item, select_all_item;

    toplevel = XtVaAppInitialize(&app, "XmTextEditor", NULL, 0, &argc, argv, NULL, NULL);
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
    YkResourceSysInit(toplevel);
    text_w = XmCreateText(scrolled_w, "text_w", NULL, 0);
    XtVaSetValues(text_w,
                  XmNeditMode, XmMULTI_LINE_EDIT,
                  XmNbackground, WhitePixelOfScreen(XtScreen(toplevel)),
		  XmNuserData, YK_SKIP_THEME_FLAG,
                  NULL);

    XtManageChild(text_w);
    XtManageChild(scrolled_w);

    /* Accelerators */
    XtVaSetValues(open_item, XmNaccelerator, "Ctrl<Key>O", XmNacceleratorText, XmStringCreateLocalized("Ctrl+O"), NULL);
    XtVaSetValues(save_item, XmNaccelerator, "Ctrl<Key>S", XmNacceleratorText, XmStringCreateLocalized("Ctrl+S"), NULL);
    XtVaSetValues(quit_item, XmNaccelerator, "Ctrl<Key>Q", XmNacceleratorText, XmStringCreateLocalized("Ctrl+Q"), NULL);
    XtVaSetValues(cut_item, XmNaccelerator, "Ctrl<Key>X", XmNacceleratorText, XmStringCreateLocalized("Ctrl+X"), NULL);
    XtVaSetValues(copy_item, XmNaccelerator, "Ctrl<Key>C", XmNacceleratorText, XmStringCreateLocalized("Ctrl+C"), NULL);
    XtVaSetValues(paste_item, XmNaccelerator, "Ctrl<Key>V", XmNacceleratorText, XmStringCreateLocalized("Ctrl+V"), NULL);
    XtVaSetValues(select_all_item, XmNaccelerator, "Ctrl<Key>A", XmNacceleratorText, XmStringCreateLocalized("Ctrl+A"), NULL);

    if (!load_bundle_icon(toplevel, argv[0]))
        fprintf(stderr, "icon load failed\n");
    
    XtRealizeWidget(toplevel);
    XtAppMainLoop(app);

    return 0;
}
