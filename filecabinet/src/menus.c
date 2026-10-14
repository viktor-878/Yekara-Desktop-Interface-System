#include <stdio.h>

#include <Xm/Xm.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/CascadeB.h>
#include <Xm/Separator.h>

#include "menus.h"
#include "actions.h"
#include "nav.h"
#include "state.h"

static Widget add_menu(Widget bar, const char *title)
{
    char pane_name[64];
    snprintf(pane_name, sizeof(pane_name), "%sPane", title);

    Widget pane = XmCreatePulldownMenu(bar, pane_name, NULL, 0);
    XtVaCreateManagedWidget(title, xmCascadeButtonWidgetClass, bar,
                            XmNsubMenuId, pane, NULL);
    return pane;
}

static Widget add_item(Widget pane, const char *label,
                       const char *accel, const char *accel_text,
                       XtCallbackProc cb, XtPointer data)
{
    Widget w = XtVaCreateManagedWidget(label, xmPushButtonWidgetClass, pane, NULL);

    if (accel) {
        XmString t = XmStringCreateLocalized((char *)accel_text);
        XtVaSetValues(w, XmNaccelerator, accel, XmNacceleratorText, t, NULL);
        XmStringFree(t);
    }

    XtAddCallback(w, XmNactivateCallback, cb, data);
    return w;
}

Widget create_menu_bar(Widget parent)
{
    Widget menuBar = XmCreateMenuBar(parent, "menuBar", NULL, 0);
    XtVaSetValues(menuBar,
        XmNtopAttachment,   XmATTACH_FORM,
        XmNleftAttachment,  XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_FORM,
        NULL);
    XtManageChild(menuBar);

    Widget filePane = add_menu(menuBar, "File");
    add_item(filePane, "Open",          "<Key>Return", "Enter",  context_menu_cb, (XtPointer)"open");
    add_item(filePane, "Show App Contents", NULL, NULL, context_menu_cb, (XtPointer)"contents");
    add_item(filePane, "New Folder...", "Ctrl<Key>n",  "Ctrl+N", new_folder_cb,   NULL);
    add_item(filePane, "Refresh",       "<Key>F5",     "F5",     refresh_cb,      NULL);
    XtVaCreateManagedWidget("fileSep", xmSeparatorWidgetClass, filePane, NULL);
    add_item(filePane, "Exit",          "Ctrl<Key>q",  "Ctrl+Q", exit_cb,         NULL);

    Widget editPane = add_menu(menuBar, "Edit");
    add_item(editPane, "Cut",           "Ctrl<Key>x",  "Ctrl+X", context_menu_cb, (XtPointer)"move");
    add_item(editPane, "Copy",          "Ctrl<Key>c",  "Ctrl+C", context_menu_cb, (XtPointer)"copy");
    add_item(editPane, "Paste",         "Ctrl<Key>v",  "Ctrl+V", canvas_menu_cb,  NULL);
    XtVaCreateManagedWidget("editSep1", xmSeparatorWidgetClass, editPane, NULL);
    add_item(editPane, "Rename...",     "<Key>F2",     "F2",     context_menu_cb, (XtPointer)"rename");
    add_item(editPane, "Delete...",     "<Key>Delete", "Del",    context_menu_cb, (XtPointer)"delete");
    XtVaCreateManagedWidget("editSep2", xmSeparatorWidgetClass, editPane, NULL);
    add_item(editPane, "Properties...", "Alt<Key>Return", "Alt+Enter", context_menu_cb, (XtPointer)"properties");

    Widget viewPane = add_menu(menuBar, "View");
    add_item(viewPane, "Icon View", NULL, NULL, icon_view_cb, NULL);
    add_item(viewPane, "List View", NULL, NULL, list_view_cb, NULL);
    XtVaCreateManagedWidget("viewSep", xmSeparatorWidgetClass, viewPane, NULL);
    {
        XmString t = XmStringCreateLocalized("Ctrl+H");
        Widget hid = XtVaCreateManagedWidget("Show Hidden Files",
                        xmToggleButtonWidgetClass, viewPane,
                        XmNaccelerator, "Ctrl<Key>h",
                        XmNacceleratorText, t,
                        NULL);
        XmStringFree(t);
        XtAddCallback(hid, XmNvalueChangedCallback, toggle_hidden_cb, NULL);
    }

    Widget goPane = add_menu(menuBar, "Go");
    add_item(goPane, "Back",    "Alt<Key>Left",  "Alt+Left",  go_back_cb,    NULL);
    add_item(goPane, "Forward", "Alt<Key>Right", "Alt+Right", go_forward_cb, NULL);
    add_item(goPane, "Up",      "Alt<Key>Up",    "Alt+Up",    go_up_cb,      NULL);
    add_item(goPane, "Home",    "Alt<Key>Home",  "Alt+Home",  go_home_cb,    NULL);

    return menuBar;
}

void create_popup_menus(Widget parent)
{
    /* Context menu for a selected item */
    item_popup_menu = XmCreatePopupMenu(parent, "itemPopupMenu", NULL, 0);
    Widget mOpen     = XtVaCreateManagedWidget("Open",          xmPushButtonWidgetClass, item_popup_menu, NULL);
    Widget mContents = XtVaCreateManagedWidget("Show App Contents", xmPushButtonWidgetClass, item_popup_menu, NULL);
    XtVaCreateManagedWidget("itemSep1", xmSeparatorWidgetClass, item_popup_menu, NULL);
    Widget mRename   = XtVaCreateManagedWidget("Rename...",     xmPushButtonWidgetClass, item_popup_menu, NULL);
    Widget mCut      = XtVaCreateManagedWidget("Cut",           xmPushButtonWidgetClass, item_popup_menu, NULL);
    Widget mCopy     = XtVaCreateManagedWidget("Copy",          xmPushButtonWidgetClass, item_popup_menu, NULL);
    XtVaCreateManagedWidget("itemSep2", xmSeparatorWidgetClass, item_popup_menu, NULL);
    Widget mDel      = XtVaCreateManagedWidget("Delete...",     xmPushButtonWidgetClass, item_popup_menu, NULL);
    XtVaCreateManagedWidget("itemSep3", xmSeparatorWidgetClass, item_popup_menu, NULL);
    Widget mProps    = XtVaCreateManagedWidget("Properties...", xmPushButtonWidgetClass, item_popup_menu, NULL);

    XtAddCallback(mOpen,     XmNactivateCallback, context_menu_cb, (XtPointer)"open");
    XtAddCallback(mContents, XmNactivateCallback, context_menu_cb, (XtPointer)"contents");
    XtAddCallback(mRename,   XmNactivateCallback, context_menu_cb, (XtPointer)"rename");
    XtAddCallback(mCut,      XmNactivateCallback, context_menu_cb, (XtPointer)"move");
    XtAddCallback(mCopy,     XmNactivateCallback, context_menu_cb, (XtPointer)"copy");
    XtAddCallback(mDel,      XmNactivateCallback, context_menu_cb, (XtPointer)"delete");
    XtAddCallback(mProps,    XmNactivateCallback, context_menu_cb, (XtPointer)"properties");

    /* Context menu for empty canvas */
    canvas_popup_menu_w = XmCreatePopupMenu(parent, "canvasPopupMenu", NULL, 0);

    Widget mPaste     = XtVaCreateManagedWidget("Paste Item Here", xmPushButtonWidgetClass, canvas_popup_menu_w, NULL);
    XtVaCreateManagedWidget("canvasSep1", xmSeparatorWidgetClass, canvas_popup_menu_w, NULL);
    Widget mNewFolder = XtVaCreateManagedWidget("New Folder...",   xmPushButtonWidgetClass, canvas_popup_menu_w, NULL);
    Widget mRefresh   = XtVaCreateManagedWidget("Refresh",         xmPushButtonWidgetClass, canvas_popup_menu_w, NULL);

    XtAddCallback(mPaste,     XmNactivateCallback, canvas_menu_cb, NULL);
    XtAddCallback(mNewFolder, XmNactivateCallback, new_folder_cb,  NULL);
    XtAddCallback(mRefresh,   XmNactivateCallback, refresh_cb,     NULL);
}
