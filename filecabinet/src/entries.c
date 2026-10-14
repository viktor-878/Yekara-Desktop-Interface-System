#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Xm/Xm.h>
#include <Xm/IconG.h>

#include "entries.h"
#include "labels.h"

void free_payload_destroy_cb(Widget w, XtPointer client_data, XtPointer call_data)
{
    free(client_data);
}

void destroy_all_children(Widget container)
{
    if (!container)
        return;

    WidgetList children;
    Cardinal num_children = 0;

    XtVaGetValues(container,
                  XmNchildren, &children,
                  XmNnumChildren, &num_children,
                  NULL);

    if (num_children == 0)
        return;

    Widget *snapshot = malloc(num_children * sizeof(Widget));
    if (!snapshot)
        return;

    memcpy(snapshot, children, num_children * sizeof(Widget));

    /* XtDestroyWidget is deferred while inside a callback, so the old
     * children would linger (and be laid out / measured) next to the new
     * ones. Unmanage them first so they vanish immediately. */
    XtUnmanageChildren(snapshot, num_children);

    for (Cardinal i = 0; i < num_children; i++)
        XtDestroyWidget(snapshot[i]);

    free(snapshot);
}

Widget create_entry(Widget parent, const char *name,
                    const char *caption,
                    Pixmap icon, Pixmap mask, Boolean icon_valid,
                    unsigned char view_type,
                    Boolean place, int x, int y,
                    const char *detail1, const char *detail2,
                    int label_max_width, int label_max_lines)
{
    Arg args[16];
    int n = 0;

    XmString   label;
    XmFontList fl = (label_max_width > 0) ? label_fontlist() : NULL;

    if (fl) {
        char shaped[1024];

        if (label_max_lines <= 1)
            ellipsize_text(fl, caption, label_max_width, shaped, sizeof(shaped));
        else
            wrap_label(fl, caption, label_max_width, label_max_lines, shaped, sizeof(shaped));

        label = XmStringCreateLtoR(shaped, XmFONTLIST_DEFAULT_TAG);
    } else {
        label = XmStringCreateLocalized((char *)caption);
    }

    char *name_copy = strdup(name);

    XtSetArg(args[n], XmNlabelString, label);             n++;
    XtSetArg(args[n], XmNviewType, view_type);            n++;
    XtSetArg(args[n], XmNuserData, (XtPointer)name_copy); n++;

    if (icon_valid) {
        XtSetArg(args[n], XmNlargeIconPixmap, icon); n++;
        XtSetArg(args[n], XmNsmallIconPixmap, icon); n++;
        if (mask != None) {
            XtSetArg(args[n], XmNlargeIconMask, mask); n++;
            XtSetArg(args[n], XmNsmallIconMask, mask); n++;
        }
    }

    if (place) {
        XtSetArg(args[n], XmNx, (Position)x); n++;
        XtSetArg(args[n], XmNy, (Position)y); n++;
    }

    Widget g = XtCreateManagedWidget("entry", xmIconGadgetClass, parent, args, n);
    XmStringFree(label);

    XtAddCallback(g, XmNdestroyCallback, free_payload_destroy_cb, (XtPointer)name_copy);

    if (detail1 && detail2) {
        XmString details[2];
        details[0] = XmStringCreateLocalized((char *)detail1);
        details[1] = XmStringCreateLocalized((char *)detail2);

        XtVaSetValues(g, XmNdetail, details, XmNdetailCount, 2, NULL);

        XmStringFree(details[0]);
        XmStringFree(details[1]);
    }

    return g;
}

void entry_name_of(Widget entry, char *out, size_t n)
{
    XtPointer nm = NULL;
    out[0] = '\0';
    if (entry)
        XtVaGetValues(entry, XmNuserData, &nm, NULL);
    if (nm)
        snprintf(out, n, "%s", (const char *)nm);
}
