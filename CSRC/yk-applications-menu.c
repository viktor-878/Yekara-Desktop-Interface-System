#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>


#define NAME_MAX_LEN 256
#define PATH_MAX_LEN 4096

// Recursively scan a directory and print FVWM AddToMenu commands
void scan_path(const char *base_path, const char *menu_id_prefix) {
    DIR *dir = opendir(base_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[PATH_MAX_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            size_t len = strlen(entry->d_name);

            if (len > 8 && strcmp(entry->d_name + len - 8, ".deskapp") == 0) {
                // It's a .deskapp → add to current menu
                char display_name[NAME_MAX_LEN];
                strncpy(display_name, entry->d_name, len - 8); // strip ".deskapp"
                display_name[len - 8] = '\0';

                if (menu_id_prefix && strlen(menu_id_prefix) > 0) {
                    printf("AddToMenu YKMenu%s \"%s\" Exec exec yk-runner \"%s\"\n",
                           menu_id_prefix, display_name, entry->d_name);
                } else {
                    printf("AddToMenu YKMenu \"%s\" Exec exec yk-runner \"%s\"\n",
                           display_name, entry->d_name);
                }

            } else {
                // It's a folder → make a submenu
                char submenu_id[NAME_MAX_LEN];
                strncpy(submenu_id, entry->d_name, NAME_MAX_LEN);
                submenu_id[NAME_MAX_LEN-1] = '\0';
		
                // convert spaces to underscores for FVWM menu identifiers
                for (char *p = submenu_id; *p; p++)
                    if (*p == ' ') *p = '_';

                char combined_id[NAME_MAX_LEN] = "";
                if (menu_id_prefix && strlen(menu_id_prefix) > 0) {
                    snprintf(combined_id, NAME_MAX_LEN, "%s_%s", menu_id_prefix, submenu_id);
                    printf("AddToMenu YKMenu%s \"%s\" Popup YKMenu%s\n",
                           menu_id_prefix, entry->d_name, combined_id);
                } else {
                    strncpy(combined_id, submenu_id, NAME_MAX_LEN);
                    combined_id[NAME_MAX_LEN-1] = '\0';
		    

                    printf("AddToMenu YKMenu \"%s\" Popup YKMenu%s\n",
                           entry->d_name, combined_id);
                }
		printf("DestroyMenu YKMenu%s \n", combined_id);
                // Recurse into folder
                scan_path(full_path, combined_id);
            }
        }
    }

    closedir(dir);
}

int main() {
    char *paths_env = getenv("YK_APPS_PATH");
    if (!paths_env) {
        fprintf(stderr, "YK_APPS_PATH not set\n");
        return 1;
    }

    char env_copy[4096];
    strncpy(env_copy, paths_env, sizeof(env_copy));
    env_copy[sizeof(env_copy)-1] = '\0';

    printf("DestroyMenu YKMenu\n");
    char *token = strtok(env_copy, ":");
    while (token) {
        char base_path[PATH_MAX_LEN];
        if (token[0] == '~') {
            const char *home = getenv("HOME");
            if (!home) home = ".";
            snprintf(base_path, PATH_MAX_LEN, "%s%s", home, token + 1);
        } else {
            strncpy(base_path, token, PATH_MAX_LEN);
            base_path[PATH_MAX_LEN-1] = '\0';
        }

        scan_path(base_path, "");

        token = strtok(NULL, ":");
    }

    return 0;
}
