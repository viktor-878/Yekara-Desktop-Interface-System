#ifndef FM_DESKAPP_H
#define FM_DESKAPP_H

#include <stddef.h>
#include <Xm/Xm.h>

#define DESKAPP_SUFFIX        ".deskapp"
#define DESKAPP_METADATA      "app.metadata"
#define DESKAPP_RESOURCES_DIR "resources"

Boolean has_deskapp_suffix(const char *name);
Boolean is_deskapp_bundle(const char *path);

/* Reads ExecFile from [Application Entry]; 0 on success. */
int deskapp_read_exec(const char *bundle, char *out, size_t out_n);

/* Reads the Icon= multi-value (file + tag); 0 on success. */
int deskapp_read_icon(const char *bundle,
                      char *out_file, size_t out_file_n,
                      char *out_tag,  size_t out_tag_n);

/* Rejects absolute paths and any ".." component. */
Boolean bundle_relative_path_is_safe(const char *rel);

#endif /* FM_DESKAPP_H */
