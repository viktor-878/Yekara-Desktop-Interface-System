#include <stdio.h>
#include <string.h>
#include <X11/Xresource.h>
#include <Xm/XmAll.h>
#include <X11/Xatom.h>
#include "yk/style.h"

static Atom YkResourceAtom;

/* ----------------------------------------------------------
   Theme structure
---------------------------------------------------------- */
typedef struct {
    Pixel background;
    Pixel foreground;
    Pixel highlight;
    Pixel shadow;
    int valid;
} YkTheme;

static YkTheme YkCurrentTheme;

/* ----------------------------------------------------------
   XRDB loader (fresh every reload)
---------------------------------------------------------- */
static XrmDatabase YkGetResourceDatabase() {
    FILE *fp;
    char buffer[65536] = {0};
    size_t len;

    fp = popen("xrdb -query", "r");
    if (!fp) {
        printf("[Yk] failed to execute xrdb\n");
        return NULL;
    }

    len = fread(buffer, 1, sizeof(buffer) - 1, fp);
    buffer[len] = '\0';
    pclose(fp);

    return XrmGetStringDatabase(buffer);
}

/* ----------------------------------------------------------
   Color conversion helpers
---------------------------------------------------------- */
static void YkQueryRGB(Display *dpy, Pixel p, int *r, int *g, int *b) {
    XColor c;
    c.pixel = p;
    XQueryColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &c);
    *r = c.red >> 8;
    *g = c.green >> 8;
    *b = c.blue >> 8;
}

static Pixel YkAllocRGB(Display *dpy, int r, int g, int b) {
    XColor c;
    Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));
    c.red   = r << 8;
    c.green = g << 8;
    c.blue  = b << 8;

    if (XAllocColor(dpy, cmap, &c)) {
        return c.pixel;
    }
    return BlackPixel(dpy, DefaultScreen(dpy));
}

static void YkComputeBevels(Display *dpy, Pixel bg, Pixel *hi, Pixel *sh) {
    int r, g, b;
    YkQueryRGB(dpy, bg, &r, &g, &b);

    int hr, hg, hb;
    int sr, sg, sb;
    int brightness = (r * 299 + g * 587 + b * 114) / 10000;

    if (brightness >= 85) {
        hr = r * 0.80; hg = g * 0.80; hb = b * 0.80;
        sr = r * 0.60; sg = g * 0.60; sb = b * 0.60;
    } else if (brightness <= 15) {
        hr = r + (255 - r) * 0.80; hg = g + (255 - g) * 0.80; hb = b + (255 - b) * 0.80;
        sr = r + (255 - r) * 0.60; sg = g + (255 - g) * 0.60; sb = b + (255 - b) * 0.60;
    } else {
        float highlight_strength = 0.59;
        float shadow_strength = 0.4375;
        hr = r + (255 - r) * highlight_strength;
        hg = g + (255 - g) * highlight_strength;
        hb = b + (255 - b) * highlight_strength;
        sr = r * (1 - shadow_strength);
        sg = g * (1 - shadow_strength);
        sb = b * (1 - shadow_strength);
    }

    *hi = YkAllocRGB(dpy, hr, hg, hb);
    *sh = YkAllocRGB(dpy, sr, sg, sb);
}

static YkTheme YkBuildTheme(Display *dpy) {
    XrmDatabase db;
    XrmValue value;
    char *type;
    YkTheme t;
    memset(&t, 0, sizeof(t));

    db = YkGetResourceDatabase();
    if (!db) return t;

    if (XrmGetResource(db, "*background", "*Background", &type, &value)) {
        t.background = YkAllocRGB(dpy, 0, 0, 0);
        sscanf((char *)value.addr, "#%lx", &t.background);
    }
    if (XrmGetResource(db, "*foreground", "*Foreground", &type, &value)) {
        sscanf((char *)value.addr, "#%lx", &t.foreground);
    }

    YkComputeBevels(dpy, t.background, &t.highlight, &t.shadow);
    t.valid = 1;
    return t;
}

/* ----------------------------------------------------------
   Theme application logic
---------------------------------------------------------- */

static void YkApplyTheme(Widget w, YkTheme *t) {
    if (!w || !t || !t->valid) return;

    XtPointer user_data = NULL;
    XtVaGetValues(w, XmNuserData, &user_data, NULL);

    /* If this widget is marked to protect its background, skip it */
    if (user_data == YK_SKIP_THEME_FLAG) {
        printf("[Yk] Skipping theme override for protected widget: %s\n", XtName(w));
        return; 
    }

    XtVaSetValues(w,
        XmNbackground, t->background,
        XmNarmColor, t->background,
        XmNforeground, t->foreground,
        XmNtopShadowColor, t->highlight,
        XmNbottomShadowColor, t->shadow,
        NULL
    );
    XmUpdateDisplay(w);
}

static void YkApplyThemeRecursive(Widget w, YkTheme *t) {
    int i;
    Widget *children;
    Cardinal num_children;

    if (!w || !t || !t->valid) return;
    YkApplyTheme(w, t);

    if (XtIsComposite(w)) {
        XtVaGetValues(w, XmNchildren, &children, XmNnumChildren, &num_children, NULL);
        for (i = 0; i < num_children; i++) {
            YkApplyThemeRecursive(children[i], t);
        }
    }
}

void YkReloadResources(Widget winTopLevel) {
    if (!winTopLevel) return;
    Display *dpy = XtDisplay(winTopLevel);

    printf("[Yk] Re-indexing XRDB & updating interface tree\n");
    YkCurrentTheme = YkBuildTheme(dpy);
    YkApplyThemeRecursive(winTopLevel, &YkCurrentTheme);
}

/* ----------------------------------------------------------
   XEvent IPC Management
---------------------------------------------------------- */

/* Custom XEvent Handler callback */
void YkPropertyHandler(Widget w, XtPointer client_data, XEvent *event, Boolean *cont) {
    (void)client_data;
    *cont = True; /* Allow other event handlers access to this event context */

    if (event->type == ClientMessage) {
        if (event->xclient.message_type == YkResourceAtom) {
            printf("[Yk] ClientMessage caught on Window ID: 0x%lx\n", event->xclient.window);
            
            /* Resolve operational Widget structure out of incoming native Window ID */
            Widget targetWidget = XtWindowToWidget(event->xclient.display, event->xclient.window);
            if (targetWidget) {
                YkReloadResources(targetWidget);
            } else {
                /* Safe fallback context */
                YkReloadResources(w);
            }
        }
    }
}

/* Subscribes a local UI frame pipeline into the event signal ecosystem */
void YkResourceSysInit(Widget winTopLevel) {
    Display *dpy = XtDisplay(winTopLevel);

    YkResourceAtom = XInternAtom(dpy, "_YK_RESOURCE_SYSTEM", False);

    /* Enforce widget generation status to guarantee clear Window IDs */
    if (!XtIsRealized(winTopLevel)) {
        XtRealizeWidget(winTopLevel);
    }

    Window win = XtWindow(winTopLevel);

    /* Target ClientMessages explicitly via Nonmaskable configuration setup */
    XtAddEventHandler(
        winTopLevel,
        NoEventMask,       /* ClientMessages carry no standard bitmask flags */
        True,              /* Catching unmaskable/nonmaskable protocols */
        YkPropertyHandler,
        NULL
    );

    /* Run baseline render pass configuration */
    YkReloadResources(winTopLevel);
    printf("[Yk] Registered Client Window ID: 0x%lx\n", win);
}

/* Global broadcaster via targeted Window Tree Walk */
void YkSendReloadEvent(Display *display) {
    XClientMessageEvent ev;
    Window root = DefaultRootWindow(display);
    Window root_return, parent_return;
    Window *children = NULL;
    unsigned int num_children = 0;

    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.display = display;
    ev.message_type = YkResourceAtom;
    ev.format = 32;

    printf("[Yk] Requesting immediate child windows from display root...\n");

    /* Query only immediate root dependencies where top-level containers reside */
    if (XQueryTree(display, root, &root_return, &parent_return, &children, &num_children)) {
        unsigned int i;
        for (i = 0; i < num_children; i++) {
            /* Hand-deliver to specific top-level targets to prevent event loss */
            ev.window = children[i];
            XSendEvent(display, children[i], False, NoEventMask, (XEvent *)&ev);
        }

        if (children) {
            XFree(children);
        }
    }

    /* Force queue dispatch to runtime display engine instantly */
    XFlush(display);
    printf("[Yk] Broadcast loop execution completed to %u root children.\n", num_children);
}
