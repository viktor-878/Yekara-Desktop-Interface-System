#ifndef FM_FSOPS_H
#define FM_FSOPS_H

#include "types.h"

/* Returns item count; *out_list is malloc'd (caller frees), sorted dirs-first. */
int scan_directory(const char *path, FileItem **out_list);

int copy_recursive(const char *src, const char *dst);
int remove_recursive(const char *path);

#endif /* FM_FSOPS_H */
