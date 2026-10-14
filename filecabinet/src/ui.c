#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/ScrolledW.h>
#include <Xm/Container.h>

#include "ui.h"
#include "callbacks.h"
#include "config.h"
#include "menus.h"
#include "state.h"
#include "view.h"

static Widget create_breadcrumb_bar(Widget parent, Widget menuBar)
{
    Widget urlOuterForm = XmCreateForm(parent, "urlOuterForm", NULL, 0);
    XtVaSetValues(urlOuterForm,
        XmNtopAttachment,   XmATTACH_WIDGET,
        XmNtopWidget,       menuBar,
        XmNleftAttachment,  XmATTACH_FORM,
        XmNrightAttachment, XmATTACH_FORM,
        XmNheight, URL_ITEM_HEIGHT + (URL_MARGIN * 2) + 4,
        XmNshadowType, XmSHADOW_OUT,
        XmNshadowThickness, 2,
        XmNrecomputeSize, False,
        NULL);
    XtManageChild(urlOuterForm);

    breadcrumbRow = XmCreateContainer(urlOuterForm, "breadcrumbRow", NULL, 0);
    XtVaSetValues(breadcrumbRow,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNtopOffset,        URL_MARGIN,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNbottomOffset,     URL_MARGIN,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNleftOffset,       URL_MARGIN,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNrightOffset,      URL_MARGIN,
        XmNlayoutType,       XmSPATIAL,
        XmNspatialStyle,     XmNONE,
        XmNentryViewType,    XmLARGE_ICON,
        XmNselectionPolicy,  XmBROWSE_SELECT,
        XmNautomaticSelection, XmAUTO_SELECT,
        NULL);
    XtAddCallback(breadcrumbRow, XmNselectionCallback, breadcrumb_select_cb, NULL);
    XtManageChild(breadcrumbRow);
    XtAddEventHandler(breadcrumbRow, StructureNotifyMask, False,
                      breadcrumb_resize_handler, NULL);

    return urlOuterForm;
}

static Widget create_status_bar(Widget parent)
{
    Widget statusForm = XmCreateForm(parent, "statusForm", NULL, 0);
    XtVaSetValues(statusForm,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        NULL);
    XtManageChild(statusForm);

    statusLabel = XmCreateLabel(statusForm, "statusLabel", NULL, 0);
    XtVaSetValues(statusLabel,
        XmNtopAttachment,    XmATTACH_FORM,
        XmNbottomAttachment, XmATTACH_FORM,
        XmNleftAttachment,   XmATTACH_FORM,
        XmNrightAttachment,  XmATTACH_FORM,
        XmNrecomputeSize,    False,
        XmNalignment,        XmALIGNMENT_BEGINNING,
        NULL);
    XtManageChild(statusLabel);

    return statusForm;
}

static void create_file_container(Widget parent, Widget top, Widget bottom)
{
    Arg args[8];
    int n = 0;

    XtSetArg(args[n], XmNtopAttachment,    XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget,        top);             n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNbottomWidget,     bottom);          n++;
    XtSetArg(args[n], XmNleftAttachment,   XmATTACH_FORM);   n++;
    XtSetArg(args[n], XmNrightAttachment,  XmATTACH_FORM);   n++;
    XtSetArg(args[n], XmNscrollingPolicy,  XmAUTOMATIC);     n++;

    scrolledWindowFrame = XmCreateScrolledWindow(parent, "scrolledWindowFrame", args, n);
    XtManageChild(scrolledWindowFrame);

    gridContainer = XmCreateContainer(scrolledWindowFrame, "gridContainer", NULL, 0);
    XtVaSetValues(gridContainer,
        XmNselectionPolicy,    XmBROWSE_SELECT,
        XmNautomaticSelection, XmAUTO_SELECT,
        XmNmarginWidth,        URL_MARGIN,
        XmNmarginHeight,       URL_MARGIN,
        NULL);

    /* Detail (list view) column headings and tab stops */
    {
        XmString headings[2];
        headings[0] = XmStringCreateLocalized("Size");
        headings[1] = XmStringCreateLocalized("Modified");

        XmTab tabs[2];
        tabs[0] = XmTabCreate((float)(LIST_NAME_MAX_WIDTH + 24), XmPIXELS, XmABSOLUTE, XmALIGNMENT_BEGINNING, ".");
        tabs[1] = XmTabCreate(110.0, XmPIXELS, XmRELATIVE, XmALIGNMENT_BEGINNING, ".");
        XmTabList tl = XmTabListInsertTabs(NULL, tabs, 2, 0);

        XtVaSetValues(gridContainer,
            XmNdetailColumnHeading,      headings,
            XmNdetailColumnHeadingCount, 2,
            XmNdetailTabList,            tl,
            NULL);

        XmStringFree(headings[0]);
        XmStringFree(headings[1]);
        XmTabFree(tabs[0]);
        XmTabFree(tabs[1]);
        XmTabListFree(tl);
    }

    XtAddCallback(gridContainer, XmNselectionCallback,      container_select_cb,         NULL);
    XtAddCallback(gridContainer, XmNdefaultActionCallback,  container_default_action_cb, NULL);
    XtAddCallback(gridContainer, XmNpopupHandlerCallback,   container_popup_cb,          NULL);
    XtManageChild(gridContainer);

    /* Reflow icons when the viewport resizes */
    {
        Widget clip = NULL;
        XtVaGetValues(scrolledWindowFrame, XmNclipWindow, &clip, NULL);
        if (clip)
            XtAddEventHandler(clip, StructureNotifyMask, False, viewport_resize_handler, NULL);
    }
}

void build_main_ui(Widget toplevel)
{
    Widget mainMasterForm = XmCreateForm(toplevel, "mainMasterForm", NULL, 0);
    XtManageChild(mainMasterForm);

    Widget menuBar       = create_menu_bar(mainMasterForm);
    Widget urlOuterForm  = create_breadcrumb_bar(mainMasterForm, menuBar);
    Widget statusForm    = create_status_bar(mainMasterForm);

    create_file_container(mainMasterForm, urlOuterForm, statusForm);
    create_popup_menus(mainMasterForm);
}
