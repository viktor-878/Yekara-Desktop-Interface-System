#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "launch.h"
#include "deskapp.h"
#include "util.h"

Boolean launch_deskapp(const char *bundle, char *err, size_t err_n)
{
    char rel[PATH_MAX];

    if (deskapp_read_exec(bundle, rel, sizeof(rel)) != 0) {
        snprintf(err, err_n, "app.metadata has no ExecFile under [Application Entry].");
        return False;
    }

    if (!bundle_relative_path_is_safe(rel)) {
        snprintf(err, err_n, "ExecFile must be a relative path inside the bundle.");
        return False;
    }

    char exe[PATH_MAX * 2];
    struct stat st;
    join_path(exe, sizeof(exe), bundle, rel);

    if (stat(exe, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(err, err_n, "Executable \"%s\" was not found in the bundle.", rel);
        return False;
    }

    if (access(exe, X_OK) != 0) {
        snprintf(err, err_n, "\"%s\" is not marked executable (try chmod +x).", rel);
        return False;
    }

    int pfd[2];
    if (pipe(pfd) != 0) {
        snprintf(err, err_n, "Could not launch: %s", strerror(errno));
        return False;
    }
    fcntl(pfd[1], F_SETFD, FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        snprintf(err, err_n, "Could not launch: %s", strerror(errno));
        close(pfd[0]);
        close(pfd[1]);
        return False;
    }

    if (pid == 0) {
        close(pfd[0]);
        setsid();
        signal(SIGCHLD, SIG_DFL);

        if (chdir(bundle) != 0) {
            int e = errno;
            ssize_t w = write(pfd[1], &e, sizeof(e));
            (void)w;
            _exit(127);
        }

        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
            if (fd > 2) close(fd);
        }

        extern char **environ;
        execve(exe, (char *[]){exe, NULL}, environ);

        int e = errno;
        ssize_t w = write(pfd[1], &e, sizeof(e));
        (void)w;
        _exit(127);
    }

    close(pfd[1]);

    int child_errno = 0;
    ssize_t r;
    do {
        r = read(pfd[0], &child_errno, sizeof(child_errno));
    } while (r < 0 && errno == EINTR);

    close(pfd[0]);

    if (r == (ssize_t)sizeof(child_errno)) {
        snprintf(err, err_n, "Could not start application: %s", strerror(child_errno));
        return False;
    }

    return True;
}

void open_with_default_app(const char *path)
{
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        signal(SIGCHLD, SIG_DFL);
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, 0); dup2(fd, 1); dup2(fd, 2);
            if (fd > 2) close(fd);
        }
        execlp("yk-open-file", "yk-open-file", path, (char *)NULL);
        _exit(127);
    }
}
