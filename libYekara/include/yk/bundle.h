#ifndef LIBykAPP_H
#define LIBykAPP_H

#include <stddef.h>

#define PATH_MAX_LEN 1024
#define NAME_MAX_LEN 256

typedef struct {
    char exec_path[PATH_MAX_LEN];
    char icon_path[PATH_MAX_LEN];
    char name[NAME_MAX_LEN];
} ykApp;

/**
 * Parses a yk app bundle.
 * Returns 0 on success, non-zero on failure.
 */
int yk_parse(const char *bundle_path, ykApp *app);

/**
 * Gets a property value from an app bundle's metadata.
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
 * Lists all yk apps found in directories specified by an environment variable.
 *   env_var: name of the environment variable (e.g., "SOL_APPS_PATH")
 *   apps: array of ykApp structs to fill
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