#ifndef FM_UTIL_H
#define FM_UTIL_H

#include <stddef.h>
#include <sys/types.h>
#include <Xm/Xm.h>
#include "types.h"

void    join_path(char *out, size_t n, const char *dir, const char *name);
Boolean path_exists(const char *p);
int     ends_with(const char *s, const char *suffix);
void    make_unique_path(const char *dir, const char *name, char *out, size_t n);
void    format_size(off_t sz, char *out, size_t n);
void    describe_item(const FileItem *f, char *size_out, size_t sn,
                      char *date_out, size_t dn);

#endif /* FM_UTIL_H */
