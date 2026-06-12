#ifndef GZI_H
#define GZI_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GZI_Image GZI_Image;

// ---------------- Loaders ----------------

// Load a GZI image (archive `.gzi` or directory-based)
GZI_Image *gzi_load(const char *path, const char *array_name);

// Free a GZI image
void gzi_free(GZI_Image *img);

// ---------------- XPM Builder ----------------

// Build XPM data suitable for XpmCreatePixmapFromData
char **gzi_build_xpm(GZI_Image *img);

// Optional: write an XPM file for testing/debug
int gzi_write_xpm(GZI_Image *img, const char *filename);

#endif // GZI_H