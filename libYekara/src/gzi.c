#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <archive.h>
#include <archive_entry.h>

typedef struct {
    char symbol[8];
    char color[32];
} GZI_Color;

struct GZI_Image {
    char array_name[64];
    int width;
    int height;
    int cpp;
    int color_count;
    GZI_Color *colors;
    char palette_file[256];
    char pxmap_file[256];
    char **pixels;
};
typedef struct GZI_Image GZI_Image;

typedef struct {
    char array_name[64];
    int width;
    int height;
    char palette_file[256];
    char pxmap_file[256];
} GZI_Section;

typedef struct {
    int section_count;
    GZI_Section *sections;
} GZI_Meta;

/* Prototypes */
void gzi_free(GZI_Image *img);
GZI_Image *gzi_load(const char *path, const char *array_name);
char **gzi_build_xpm(GZI_Image *img);

/* ---------------- Utility ---------------- */
static void trim(char *s) {
    char *p;
    if ((p = strchr(s, '\n'))) *p = 0;
    if ((p = strchr(s, '\r'))) *p = 0;
}

static int ends_with(const char *s, const char *suffix) {
    size_t sl = strlen(s), su = strlen(suffix);
    return su <= sl && strcmp(s + sl - su, suffix) == 0;
}

/* ---------------- Meta Parser ---------------- */
static GZI_Meta parse_meta_sections(char *data) {
    GZI_Meta meta = {0, NULL};
    GZI_Section current = {0};

    char *line = strtok(data, "\n");
    while(line) {
        trim(line);
        if(line[0] == '#' || line[0] == 0) { 
            line = strtok(NULL, "\n"); 
            continue; 
        }

        if(line[0] == '[') {
            if(current.array_name[0] != 0) {
                GZI_Section *tmp = realloc(meta.sections, sizeof(GZI_Section) * (meta.section_count + 1));
                if (tmp) {
                    meta.sections = tmp;
                    meta.sections[meta.section_count++] = current;
                }
            }
            memset(&current, 0, sizeof(GZI_Section));
            sscanf(line, "[%63[^]]", current.array_name);
        }
        else if(strncmp(line, "width=", 6) == 0) current.width = atoi(line + 6);
        else if(strncmp(line, "height=", 7) == 0) current.height = atoi(line + 7);
        else if(strncmp(line, "palette=", 8) == 0) snprintf(current.palette_file, sizeof(current.palette_file), "%s.opal", line + 8); // palette file name
        else if(strncmp(line, "name=", 5) == 0) snprintf(current.pxmap_file, sizeof(current.pxmap_file), "%s.pxmap", line + 5); // pxmap file name

        line = strtok(NULL, "\n");
    }

    if(current.array_name[0] != 0) {
        GZI_Section *tmp = realloc(meta.sections, sizeof(GZI_Section) * (meta.section_count + 1));
        if (tmp) {
            meta.sections = tmp;
            meta.sections[meta.section_count++] = current;
        }
    }

    return meta;
}

/* ---------------- Palette Parser (With !charPerPx) ---------------- */
static int parse_palette_mem(char *data, GZI_Image *img) {
    int count = 0;
    int first_line = 1;
    char *copy = strdup(data);
    if (!copy) return 0;

    /* First pass: Count total active color entries & extract header config */
    char *line = strtok(copy, "\n");
    while(line) { 
        trim(line); 
        if(line[0] != 0) {
            if (first_line && strncmp(line, "!charPerPx ", 11) == 0) {
                img->cpp = atoi(line + 11);
            } else {
                count++; 
            }
        }
        first_line = 0;
        line = strtok(NULL, "\n"); 
    }
    free(copy);

    if (img->cpp <= 0) {
        fprintf(stderr, "Invalid or missing !charPerPx definition in palette.\n");
        return 0;
    }

    img->colors = malloc(sizeof(GZI_Color) * count);
    if (!img->colors) return 0;
    img->color_count = 0;

    /* Second pass: Fully parse isolated data safely */
    char *parse_buf = strdup(data);
    if (!parse_buf) return 0;

    first_line = 1;
    line = strtok(parse_buf, "\n");
    while(line) {
        trim(line);
        if(line[0] == 0 || (first_line && strncmp(line, "!charPerPx ", 11) == 0)) { 
            first_line = 0;
            line = strtok(NULL, "\n"); 
            continue; 
        }
        first_line = 0;

        char *p = strstr(line, " c ");
        if(!p) { 
            fprintf(stderr, "Bad palette line: %s\n", line); 
            free(parse_buf);
            return 0; 
        }
        *p = 0;
        
        strncpy(img->colors[img->color_count].symbol, line, sizeof(img->colors[0].symbol) - 1);
        img->colors[img->color_count].symbol[sizeof(img->colors[0].symbol) - 1] = 0;
        
        strncpy(img->colors[img->color_count].color, p + 3, sizeof(img->colors[0].color) - 1);
        img->colors[img->color_count].color[sizeof(img->colors[0].color) - 1] = 0;
        
        img->color_count++;
        line = strtok(NULL, "\n");
    }
    free(parse_buf);
    return 1;
}

/* ---------------- Pxmap Parser ---------------- */
static int parse_pxmap_mem(char *data, GZI_Image *img) {
    img->pixels = calloc(img->height, sizeof(char*));
    if (!img->pixels) return 0;
    
    char *line = strtok(data, "\n");
    int y = 0;

    while(line && y < img->height) {
        trim(line);
        if((int)strlen(line) != img->width * img->cpp) {
            fprintf(stderr, "Line %d length mismatch (%lu expected %d)\n", y, (unsigned long)strlen(line), img->width * img->cpp);
            return 0;
        }

        for(int x = 0; x < img->width; x++) {
            char sym[16];
            int max_cpp = img->cpp > 15 ? 15 : img->cpp;
            strncpy(sym, line + (x * img->cpp), max_cpp);
            sym[max_cpp] = 0;
            
            int found = 0;
            for(int c = 0; c < img->color_count; c++) {
                if(strcmp(sym, img->colors[c].symbol) == 0) { 
                    found = 1; 
                    break; 
                }
            }
            if(!found) { 
                fprintf(stderr, "Unknown symbol at row %d: '%s'\n", y, sym); 
                return 0; 
            }
        }

        img->pixels[y] = strdup(line);
        y++;
        line = strtok(NULL, "\n");
    }
    return y == img->height;
}

/* ---------------- Archive Loader ---------------- */
static int load_from_archive(const char *file, GZI_Image *img, const char *array_name) {
    struct archive *a;
    struct archive_entry *entry;

    char *meta_data = NULL, *palette_data = NULL, *pxmap_data = NULL;

    printf("\n=== PASS 1 ===\n");

    a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_filter_gzip(a);

    if(archive_read_open_filename(a, file, 10240) != ARCHIVE_OK) {
        printf("ERROR: Could not open archive\n");
        archive_read_free(a);
        return 0;
    }

    while(archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *name = archive_entry_pathname(entry);
        size_t size = archive_entry_size(entry);

        char *buf = malloc(size + 1);
        if(!buf) continue;

        archive_read_data(a, buf, size);
        buf[size] = 0;

        const char *base_name = strrchr(name, '/');
        if(base_name) base_name++;
        else base_name = name;

        printf("PASS1 FILE: '%s'\n", base_name);

        if(ends_with(base_name, ".meta")) {
            printf("PASS1 FOUND META: '%s'\n", base_name);
            meta_data = buf;
        }
        else {
            free(buf);
        }
    }

    archive_read_free(a);

    printf("\n=== META STATUS ===\n");
    printf("meta_data = %p\n", (void*)meta_data);

    if(!meta_data) {
        printf("FAIL: meta_data is NULL\n");
        return 0;
    }

    printf("\n=== PARSING META ===\n");

    GZI_Meta meta = parse_meta_sections(meta_data);

    printf("Sections found: %d\n", meta.section_count);
    printf("Looking for array: '%s'\n", array_name);

    GZI_Section *pick = NULL;

    for(int i = 0; i < meta.section_count; i++) {
        printf("Section[%d] = '%s'\n",
               i,
               meta.sections[i].array_name);

        if(strcmp(meta.sections[i].array_name, array_name) == 0) {
            pick = &meta.sections[i];
            break;
        }
    }

    if(!pick) {
        fprintf(stderr,
                "Array name '%s' not found\n",
                array_name);

        free(meta_data);
        free(meta.sections);
        return 0;
    }

    printf("\n=== PICK ===\n");
    printf("array_name   = '%s'\n", pick->array_name);
    printf("palette_file = '%s'\n", pick->palette_file);
    printf("pxmap_file   = '%s'\n", pick->pxmap_file);
    printf("width        = %d\n", pick->width);
    printf("height       = %d\n", pick->height);

    printf("\n=== PASS 2 ===\n");

    a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_filter_gzip(a);

    if(archive_read_open_filename(a, file, 10240) != ARCHIVE_OK) {
        printf("ERROR: Could not reopen archive\n");
        archive_read_free(a);
        free(meta_data);
        free(meta.sections);
        return 0;
    }

    while(archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *name = archive_entry_pathname(entry);
        size_t size = archive_entry_size(entry);

        char *buf = malloc(size + 1);
        if(!buf) continue;

        archive_read_data(a, buf, size);
        buf[size] = 0;

        const char *base_name = strrchr(name, '/');
        if(base_name) base_name++;
        else base_name = name;

        printf("PASS2 FILE: '%s'\n", base_name);

        if(strcmp(base_name, pick->palette_file) == 0) {
            printf("MATCHED PALETTE\n");
            palette_data = buf;
        }
        else if(strcmp(base_name, pick->pxmap_file) == 0) {
            printf("MATCHED PXMAP\n");
            pxmap_data = buf;
        }
        else {
            free(buf);
        }
    }

    archive_read_free(a);

    printf("\n=== FINAL STATUS ===\n");
    printf("meta_data    = %p\n", (void*)meta_data);
    printf("palette_data = %p\n", (void*)palette_data);
    printf("pxmap_data   = %p\n", (void*)pxmap_data);

    if(!meta_data)
        printf("FAIL: meta_data missing\n");

    if(!palette_data)
        printf("FAIL: palette_data missing\n");

    if(!pxmap_data)
        printf("FAIL: pxmap_data missing\n");

    if(!meta_data || !palette_data || !pxmap_data) {
        free(meta_data);
        free(palette_data);
        free(pxmap_data);
        free(meta.sections);
        return 0;
    }

    strncpy(img->array_name,
            pick->array_name,
            sizeof(img->array_name) - 1);

    strncpy(img->palette_file,
            pick->palette_file,
            sizeof(img->palette_file) - 1);

    strncpy(img->pxmap_file,
            pick->pxmap_file,
            sizeof(img->pxmap_file) - 1);

    img->width = pick->width;
    img->height = pick->height;

    printf("\n=== IMAGE LOAD ===\n");

    int success = 0;

    printf("Parsing palette...\n");

    if(parse_palette_mem(palette_data, img)) {
        printf("Palette OK\n");

        printf("Parsing pxmap...\n");

        if(parse_pxmap_mem(pxmap_data, img)) {
            printf("Pxmap OK\n");
            success = 1;
        }
        else {
            printf("Pxmap FAILED\n");
        }
    }
    else {
        printf("Palette FAILED\n");
    }

    printf("Final success = %d\n", success);

    free(meta_data);
    free(palette_data);
    free(pxmap_data);
    free(meta.sections);

    return success;
}

/* ---------------- Public Loader ---------------- */
GZI_Image *gzi_load(const char *path, const char *array_name) {
    GZI_Image *img = calloc(1, sizeof(GZI_Image));
    if(!img) return NULL;
    
    int ok = ends_with(path, ".gzi") ? load_from_archive(path, img, array_name) : 0;
    if(!ok) { 
        gzi_free(img); 
        return NULL; 
    }
    return img;
}

/* ---------------- XPM Builder ---------------- */
char **gzi_build_xpm(GZI_Image *img) {
    if(!img) return NULL;

    int total = img->color_count + img->height + 1;
    char **xpm = malloc(sizeof(char*) * (total + 1));
    if(!xpm) return NULL;

    char header[256]; 
    snprintf(header, sizeof(header), "%d %d %d %d", img->width, img->height, img->color_count, img->cpp);
    xpm[0] = strdup(header);

    for(int i = 0; i < img->color_count; i++) {
        char line[256]; 
        snprintf(line, sizeof(line), "%s c %s", img->colors[i].symbol, img->colors[i].color);
        xpm[i + 1] = strdup(line);
    }

    for(int y = 0; y < img->height; y++)
        xpm[y + img->color_count + 1] = strdup(img->pixels[y]);

    xpm[total] = NULL;
    return xpm;
}

/* ---------------- Free ---------------- */
void gzi_free(GZI_Image *img) {
    if(!img) return;
    if(img->colors) free(img->colors);
    if(img->pixels) {
        for(int i = 0; i < img->height; i++) {
            if(img->pixels[i]) free(img->pixels[i]);
        }
        free(img->pixels);
    }
    free(img);
}
