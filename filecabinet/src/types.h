#ifndef FM_TYPES_H
#define FM_TYPES_H

#include <time.h>
#include <sys/types.h>
#include <Xm/Xm.h>

typedef struct {
    char    name[256];
    Boolean is_directory;
    Boolean is_app;
    off_t   size;
    time_t  mtime;
} FileItem;

typedef struct {
    char name[256];
    int  x;
    int  y;
} IconPosition;

#endif /* FM_TYPES_H */
