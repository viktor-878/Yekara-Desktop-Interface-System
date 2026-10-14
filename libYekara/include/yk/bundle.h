#ifndef LIBykAPP_H
#define LIBykAPP_H

#include <stddef.h>

#define PATH_MAX_LEN 1024
#define NAME_MAX_LEN 256

/* Capacity callers must provide for yk_list_apps() / yk_list_filetypes(). */
#define YK_MAX_APPS      256
#define YK_MAX_FILETYPES 256

#define YK_MAX_PROPS     16
#define YK_KEY_MAX       64
#define YK_VALUE_MAX     256
#define YK_EXT_MAX       32
#define YK_HANDLER_MAX   64

typedef struct {
    char exec_path[PATH_MAX_LEN];
    char icon_path[PATH_MAX_LEN];   /* "<bundle>/resources/<file>.gzi[:tag]" or "" */
    char name[NAME_MAX_LEN];        /* bundle directory name, e.g. "TextBook.deskapp" */
    char bundle_path[PATH_MAX_LEN]; /* absolute path of the bundle directory */
} ykApp;

/**
 * Parses a yk app bundle.
 * Only keys in the [Application Entry] section (or before any section header)
 * are read, so file type sections can never override the app's own values.
 * Returns 0 on success, non-zero on failure.
 */
int yk_parse(const char *bundle_path, ykApp *app);

/**
 * Gets a property value from an app bundle's metadata
 * ([Application Entry] section, or before any section header).
 * Returns:
 *   0 on success
 *   1 if file can't be opened
 *   2 if key not found
 */
int yk_get_property(
    const char *bundle_path,
    const char *key,
    char *out_value,
    size_t out_size
);

/**
 * Lists all yk apps found in directories specified by an environment variable
 * (colon-separated; "~", "$HOME" and "${HOME}" are expanded; directories are
 * searched recursively and in alphabetical order, so results are stable).
 *   env_var: name of the environment variable (e.g., "YK_APPS_PATH")
 *   apps: array of ykApp structs to fill; must hold YK_MAX_APPS entries
 *   app_count: pointer to int to receive number of apps found
 * Returns 0 on success, non-zero on failure.
 */
int yk_list_apps(const char *env_var, ykApp apps[], int *app_count);

/**
 * Finds a specific app bundle by name.
 * Returns 0 on success.
 */
int yk_find_app(const char *app_name, ykApp *out_app);


/* ------------------------------------------------ */
/* File types                                       */
/* ------------------------------------------------ */

/*
 * An application claims a file type with a section in its app.metadata:
 *
 *   [txt Entry]                  ; or [.txt Entry]; case-insensitive
 *   icon=filetypes.gzi:txt_icon  ; GZI file under the bundle's resources/ folder
 *   handler=open                 ; becomes  --open <file>   (default "open")
 *   theme=dark                   ; every other key becomes  --theme=dark
 *
 * Only apps found through $YK_APPS_PATH (default
 * "/applications:$HOME/applications" when unset or empty) can claim types. An
 * app whose ExecFile is missing, unsafe or not executable claims nothing.
 * If several apps claim the same extension, the first one wins.
 *
 * Handler names and property keys may only use letters, digits, '_' and '-';
 * anything else is ignored. icon= must be "<file>[.gzi]:<tag>", relative to
 * the bundle's resources/ folder (no "..").
 */
typedef struct {
    char key[YK_KEY_MAX];
    char val[YK_VALUE_MAX];
} ykProperty;

typedef struct {
    char       ext[YK_EXT_MAX];             /* lowercase, no dot */
    char       bundle_path[PATH_MAX_LEN];
    char       exec_path[PATH_MAX_LEN];     /* absolute path of ExecFile */
    char       handler[YK_HANDLER_MAX];     /* "open" -> --open */
    ykProperty props[YK_MAX_PROPS];
    int        prop_count;
    char       icon_file[PATH_MAX_LEN];     /* absolute path of the .gzi, "" if none */
    char       icon_tag[NAME_MAX_LEN];      /* tag inside that .gzi */
} ykFileType;

/**
 * Extracts the lowercase extension of a path's last component into `out`
 * ("/a/b/Photo.PNG" -> "png"). Returns 0 on success, 1 if there is none
 * (no dot, leading-dot name, trailing dot, or unusable characters).
 */
int yk_file_extension(const char *path, char *out, size_t out_size);

/**
 * Finds the application registered for an extension (with or without leading
 * dot, any case). Returns:
 *   0 found (out filled)
 *   1 no application handles it
 *   2 invalid extension
 */
int yk_find_filetype(const char *ext, ykFileType *out);

/**
 * Lists every registered file type (one entry per extension, first claim
 * wins). `list` must hold at least `max` entries (use YK_MAX_FILETYPES).
 * Returns 0 on success.
 */
int yk_list_filetypes(ykFileType list[], int max, int *count);

/**
 * When non-zero, yk_*filetype* functions print warnings about malformed
 * app.metadata entries to stderr. Off by default.
 */
void yk_set_verbose(int on);


/* ------------------------------------------------ */
/* New bundle self-inspection functions */
/* ------------------------------------------------ */

/**
 * Gets the name of the bundle that the current executable belongs to.
 * Example: "TextBook"
 * Returns 0 on success.
 */
const char* yk_get_current_bundle();

/**
 * Gets the filesystem path to the current bundle directory.
 * Example: "/opt/yk/apps/TextBook"
 * Returns 0 on success.
 */
const char* yk_get_current_bundle_path();

/**
 * Gets the icon path of the current bundle.
 * Example: ".../resources/textbook.xpm"
 * Returns 0 on success.
 */
const char* yk_get_current_bundle_icon();

void yk_set_argv0(char *argv0);

#endif
