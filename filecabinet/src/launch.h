#ifndef FM_LAUNCH_H
#define FM_LAUNCH_H

#include <stddef.h>
#include <Xm/Xm.h>

/* Runs the bundle's ExecFile; on failure fills err and returns False. */
Boolean launch_deskapp(const char *bundle, char *err, size_t err_n);

/* Starts another File Cabinet window showing `dir` (used by the desktop). */
Boolean spawn_file_manager(const char *dir, char *err, size_t err_n);

/* Hands a file to the "yk-open-file" helper. */
void open_with_default_app(const char *path);

#endif /* FM_LAUNCH_H */
