#include <limits.h>

#include <Xm/Xm.h>
#include <Xm/Container.h>
#include <Xm/IconG.h>
#include <Xm/RowColumn.h>

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

/*
 * Right-click on the file grid.
 *
 * Motif's automatic popup posting (XmNpopupHandlerCallback) only works when
 * the menu was created on the widget that receives the click.  Our container
 * lives inside a ScrolledWindow while the menus hang off the main form, so
 * the callback never ran.  We post the menus ourselves instead; the menus
 * are created with XmNpopupEnabled = XmPOPUP_DISABLED (see menus.c).
 */
void container_btn_handler(Widget w, XtPointer client, XEvent *ev, Boolean *cont)
{
    Widget target, menu;

    (void)client;
    (void)cont;

    if (ev->type != ButtonPress || ev->xbutton.button != Button3)
        return;

    target = XmObjectAtPoint(w, (Position)ev->xbutton.x, (Position)ev->xbutton.y);

    if (target && target != w && XmIsIconGadget(target)) {
        Widget sel[1];
        sel[0] = target;

        XtVaSetValues(w,
                      XmNselectedObjects, sel,
                      XmNselectedObjectCount, 1,
                      NULL);

        entry_name_of(target, selected_node_name, sizeof(selected_node_name));
        menu = item_popup_menu;
    } else {
        XtVaSetValues(w, XmNselectedObjectCount, 0, NULL);
        selected_node_name[0] = '\0';
        menu = canvas_popup_menu_w;
    }

    update_status_bar();

    if (menu) {
        XmMenuPosition(menu, &ev->xbutton);
        XtManageChild(menu);
    }
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
