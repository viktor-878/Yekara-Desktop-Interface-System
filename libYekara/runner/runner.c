#include <stdio.h>
#include <unistd.h>
#include "yk/bundle.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s AppName[.deskapp] [args...]\n", argv[0]);
        return 1;
    }

    // ---------------------- Find app ----------------------
    ykApp app;

    if (yk_find_app(argv[1], &app) != 0) {
        fprintf(stderr, "Error: App '%s' not found in YK_APPS_PATH\n", argv[1]);
        return 2;
    }

    // ---------------------- Build argument list ----------------------
    char *exec_argv[argc];

    exec_argv[0] = app.exec_path;

    for (int i = 2; i < argc; i++) {
        exec_argv[i - 1] = argv[i];
    }

    exec_argv[argc - 1] = NULL;

    // ---------------------- Execute ----------------------
    execv(app.exec_path, exec_argv);

    perror("execv");
    return 3;
}
