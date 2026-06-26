#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include "yk/bundle.h"

#define MAX_PATHS 64
#define MAX_APPS 256


// ------------------------------------------------
// Recursive directory scan
// ------------------------------------------------

static void scan_dir_recursive(const char *dir_path, ykApp apps[], int *count)
{
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && *count < MAX_APPS) {

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[PATH_MAX_LEN];
        snprintf(full_path, PATH_MAX_LEN, "%s/%s", dir_path, entry->d_name);

        if (entry->d_type == DT_DIR) {

            size_t len = strlen(entry->d_name);

            if (len > 5 && strcmp(entry->d_name + len - 8, ".deskapp") == 0) {
                if (yk_parse(full_path, &apps[*count]) == 0) {
                    (*count)++;
                }
            } else {
                scan_dir_recursive(full_path, apps, count);
            }
        }
    }

    closedir(dir);
}


// ------------------------------------------------
// App listing
// ------------------------------------------------

int yk_list_apps(const char *env_var, ykApp apps[], int *app_count) {

    char *path_env = getenv(env_var);
    if (!path_env) {
        fprintf(stderr, "Environment variable '%s' not set\n", env_var);
        return 1;
    }

    char *paths[MAX_PATHS];
    int path_idx = 0;

    char env_copy[4096];
    strncpy(env_copy, path_env, sizeof(env_copy));
    env_copy[sizeof(env_copy)-1] = '\0';

    char *token = strtok(env_copy, ":");

    while (token && path_idx < MAX_PATHS) {
        paths[path_idx++] = token;
        token = strtok(NULL, ":");
    }

    int count = 0;

    for (int i = 0; i < path_idx; i++) {

        char dir_path[PATH_MAX_LEN];

        if (paths[i][0] == '~') {
            const char *home = getenv("HOME");
            if (!home) home = ".";
            snprintf(dir_path, PATH_MAX_LEN, "%s%s", home, paths[i] + 1);
        } else {
            strncpy(dir_path, paths[i], PATH_MAX_LEN);
            dir_path[PATH_MAX_LEN-1] = '\0';
        }

        scan_dir_recursive(dir_path, apps, &count);
    }

    *app_count = count;
    return 0;
}


// ------------------------------------------------
// Metadata parsing
// ------------------------------------------------

int yk_parse(const char *bundle_path, ykApp *app) {

    char meta_path[PATH_MAX_LEN];
    snprintf(meta_path, PATH_MAX_LEN, "%s/app.metadata", bundle_path);

    FILE *meta = fopen(meta_path, "r");
    if (!meta) {
        fprintf(stderr, "Error: Metadata not found in '%s'\n", bundle_path);
        return 1;
    }

    char line[512];
    char exec_file[PATH_MAX_LEN] = "";
    char icon_file[PATH_MAX_LEN] = "";

    while (fgets(line, sizeof(line), meta)) {

        if (strncmp(line, "ExecFile=", 9) == 0) {
            strncpy(exec_file, line + 9, PATH_MAX_LEN);
            exec_file[strcspn(exec_file, "\n")] = 0;
        }
        else if (strncmp(line, "Icon=", 5) == 0) {
            strncpy(icon_file, line + 5, PATH_MAX_LEN);
            icon_file[strcspn(icon_file, "\n")] = 0;
        }
    }

    fclose(meta);

    if (strlen(exec_file) == 0) {
        fprintf(stderr, "Error: ExecFile missing in metadata\n");
        return 2;
    }

    snprintf(app->exec_path, PATH_MAX_LEN, "%s/%s", bundle_path, exec_file);

    if (strlen(icon_file) > 0) {
        snprintf(app->icon_path, PATH_MAX_LEN, "%s/resources/%s.gzi", bundle_path, icon_file);
    } else {
        app->icon_path[0] = '\0';
    }

    const char *last_slash = strrchr(bundle_path, '/');

    if (last_slash)
        strncpy(app->name, last_slash + 1, NAME_MAX_LEN);
    else
        strncpy(app->name, bundle_path, NAME_MAX_LEN);

    app->name[NAME_MAX_LEN-1] = '\0';

    return 0;
}


// ------------------------------------------------
// Metadata property reader
// ------------------------------------------------

int yk_get_property(const char *bundle_path,
                        const char *key,
                        char *out_value,
                        size_t out_size)
{
    char meta_path[PATH_MAX_LEN];
    snprintf(meta_path, PATH_MAX_LEN, "%s/app.metadata", bundle_path);

    FILE *meta = fopen(meta_path, "r");
    if (!meta) return 1;

    char line[512];
    size_t key_len = strlen(key);

    while (fgets(line, sizeof(line), meta)) {

        if (strncmp(line, key, key_len) == 0 && line[key_len] == '=') {

            strncpy(out_value, line + key_len + 1, out_size);
            out_value[strcspn(out_value, "\n")] = 0;

            fclose(meta);
            return 0;
        }
    }

    fclose(meta);
    return 2;
}


// ------------------------------------------------
// Find specific app
// ------------------------------------------------

int yk_find_app(const char *app_name, ykApp *out_app) {

    ykApp apps[MAX_APPS];
    int count = 0;

    if (yk_list_apps("YK_APPS_PATH", apps, &count) != 0)
        return 2;

    for (int i = 0; i < count; i++) {
        if (strcmp(apps[i].name, app_name) == 0) {
            *out_app = apps[i];
            return 0;
        }
    }

    char name_with_ext[NAME_MAX_LEN];
    snprintf(name_with_ext, NAME_MAX_LEN, "%s.deskapp", app_name);

    for (int i = 0; i < count; i++) {
        if (strcmp(apps[i].name, name_with_ext) == 0) {
            *out_app = apps[i];
            return 0;
        }
    }

    return 1;
}


// ------------------------------------------------
// Current bundle detection
// ------------------------------------------------

static char *argv0_ptr = NULL;

void yk_set_argv0(char *argv0) {
    argv0_ptr = argv0;
}

const char* yk_get_current_bundle_path()
{
    static char path[PATH_MAX_LEN];

    if (!argv0_ptr) return NULL;

    if (!realpath(argv0_ptr, path))
        return NULL;

    char *p = strrchr(path, '/');
    if (p) *p = '\0';

    return path;
}

const char* yk_get_current_bundle()
{
    const char *bundle_path = yk_get_current_bundle_path();
    if (!bundle_path) return NULL;

    static char bundle[NAME_MAX_LEN];

    const char *p = strrchr(bundle_path, '/');
    if (!p) return NULL;

    strncpy(bundle, p + 1, NAME_MAX_LEN);
    bundle[NAME_MAX_LEN-1] = '\0';

    return bundle;
}

const char* yk_get_current_bundle_icon()
{
    static char icon_path[PATH_MAX_LEN];

    const char *bundle_path = yk_get_current_bundle_path();
    if (!bundle_path) return NULL;

    char icon_name[NAME_MAX_LEN];

    if (yk_get_property(bundle_path, "Icon", icon_name, sizeof(icon_name)) != 0)
        return NULL;

    snprintf(icon_path, sizeof(icon_path), "%s/resources/%s.gzi", bundle_path, icon_name);

    return icon_path;
}
