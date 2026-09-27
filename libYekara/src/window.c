#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <unistd.h>

#include <Xm/Xm.h>

#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/xpm.h>

#include "yk/window.h"
#include "yk/style.h"
#include "yk/gzi.h"
#include "yk/bundle.h"


static int load_bundle_icon(Widget toplevel, char *argv0)
{
    yk_set_argv0(argv0);

    const char *icon_ref = yk_get_current_bundle_icon();

    if (!icon_ref) {
        fprintf(stderr, "GZI icon load failed\n");
        return 0;
    }

    /*
     * Icon reference format:
     *
     *     /path/to/icon.gzi:tag_name
     */

    char icon_path[PATH_MAX_LEN];
    char icon_tag[NAME_MAX_LEN];

    const char *separator = strchr(icon_ref, ':');

    if (!separator) {
        fprintf(stderr, "Invalid GZI icon reference: %s\n", icon_ref);
        return 0;
    }

    size_t path_length = separator - icon_ref;

    if (path_length >= sizeof(icon_path)) {
        fprintf(stderr, "GZI icon path too long\n");
        return 0;
    }

    memcpy(icon_path, icon_ref, path_length);
    icon_path[path_length] = '\0';

    snprintf(
        icon_tag,
        sizeof(icon_tag),
        "%s",
        separator + 1
    );

    /*
     * GZI already knows how to load a specific tagged
     * image, so pass the two pieces directly to gzi_load().
     */
    GZI_Image *img = gzi_load(icon_path, icon_tag);

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
        fprintf(
            stderr,
            "XPM build failed: %s\n",
            XpmGetErrorString(status)
        );

        gzi_free(img);
        return 0;
    }

    XtVaSetValues(
        toplevel,
        XmNiconPixmap, pixmap,
        XmNiconMask, mask,
        NULL
    );

    gzi_free(img);

    return 1;
}

/* ---------------------------------------------------------
 * Yekara Window Initialization
 * --------------------------------------------------------- */

Widget
YkInitWindow(
    XtAppContext *app,
    char *id,
    int *argc,
    char **argv
)
{
    Widget toplevel;

    /*
     * Initialize Xt/Motif.
     */
    toplevel = XtVaAppInitialize(
        app,
        id,
        NULL,
        0,
        argc,
        argv,
        NULL,
        NULL
    );
    
    if (argv && argv[0])
    load_bundle_icon(toplevel, argv[0]);

	printf("THIS WINDOW IS A VALID YKINITWINDOW YEKARA WINDOW");

    /*
     * Initialize the Yekara resource/style system.
     */
    YkResourceSysInit(toplevel);

    return toplevel;
}
