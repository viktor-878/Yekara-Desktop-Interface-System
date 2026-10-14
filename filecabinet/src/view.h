#ifndef FM_VIEW_H
#define FM_VIEW_H

#include <Xm/Xm.h>
#include <X11/Xlib.h>

/* Rebuilds breadcrumb bar and file container for current_path. */
void render_view(void);

/* Refreshes the status line from current selection/clipboard state. */
void update_status_bar(void);

/* Event handler for the scrolled window's clip window (reflow on resize). */
void viewport_resize_handler(Widget w, XtPointer client,
                             XEvent *event, Boolean *cont);

/* Event handler for the breadcrumb bar (re-fits crumbs when it resizes). */
void breadcrumb_resize_handler(Widget w, XtPointer client,
                               XEvent *event, Boolean *cont);

#endif /* FM_VIEW_H */
