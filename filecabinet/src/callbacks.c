#include <limits.h>

#include <Xm/Xm.h>
#include <Xm/Container.h>
#include <Xm/IconG.h>

#include "callbacks.h"
#include "entries.h"
#include "nav.h"
#include "state.h"
#include "view.h"

void container_select_cb(Widget w, XtPointer client, XtPointer call)
{
    XmContainerSelectCallbackStruct *cbs = (XmContainerSelectCallbackStruct *)call;

    selected_node_name[0] = '\0';
    if (cbs->selected_item_count > 0)
        entry_name_of(cbs->selected_items[0], selected_node_name, sizeof(selected_node_name));

    update_status_bar();
}

void container_default_action_cb(Widget w, XtPointer client, XtPointer call)
{
    XmContainerSelectCallbackStruct *cbs = (XmContainerSelectCallbackStruct *)call;
    char nm[256];

    if (cbs->selected_item_count < 1)
        return;

    entry_name_of(cbs->selected_items[0], nm, sizeof(nm));
    if (nm[0])
        activate_item(nm);
}

void container_popup_cb(Widget w, XtPointer client, XtPointer call)
{
    XmPopupHandlerCallbackStruct *cbs = (XmPopupHandlerCallbackStruct *)call;
    Widget target = cbs->target;

    if (target && target != w && XmIsIconGadget(target)) {
        Widget sel[1];
        sel[0] = target;

        XtVaSetValues(gridContainer,
                      XmNselectedObjects, sel,
                      XmNselectedObjectCount, 1,
                      NULL);

        entry_name_of(target, selected_node_name, sizeof(selected_node_name));
        cbs->menuToPost = item_popup_menu;
    } else {
        XtVaSetValues(gridContainer, XmNselectedObjectCount, 0, NULL);
        selected_node_name[0] = '\0';
        cbs->menuToPost = canvas_popup_menu_w;
    }

    update_status_bar();
    cbs->postIt = True;
}

void breadcrumb_select_cb(Widget w, XtPointer client, XtPointer call)
{
    XmContainerSelectCallbackStruct *cbs = (XmContainerSelectCallbackStruct *)call;
    char path[PATH_MAX];

    if (cbs->selected_item_count < 1)
        return;

    entry_name_of(cbs->selected_items[0], path, sizeof(path));
    if (path[0] == '/')
        change_directory(path);
}
