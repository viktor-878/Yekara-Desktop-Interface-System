#ifndef ENTRY_H
#define ENTRY_H

#ifdef __cplusplus
extern "C" {
#endif

// Represents a tagged multi-value (e.g., filename:tag)
typedef struct {
    char *value;
    char *tag;
} EntryMultiValue;

// Represents a single key-value entry
typedef struct EntryNode {
    char *key;
    char *raw_value;        // Full string after '='
    EntryMultiValue multi;  // Populated if a colon tag exists
    int is_multi;           // 1 if tagged/multi-value, 0 otherwise
    struct EntryNode *next;
} EntryNode;

// Represents a section (e.g., [Desktop Entry])
typedef struct EntrySection {
    char *name;
    EntryNode *entries;
    struct EntrySection *next;
} EntrySection;

// The root document structure
typedef struct {
    EntrySection *sections;
} EntryDocument;

EntryDocument *parse_entry_string(const char *content);
// Core API
EntryDocument *parse_entry_file(const char *filename);
void free_entry_document(EntryDocument *doc);

// Query Helpers
const char *get_entry_string(EntryDocument *doc, const char *section, const char *key, const char *default_val);
int get_entry_int(EntryDocument *doc, const char *section, const char *key, int default_val);
const EntryMultiValue *get_entry_multi(EntryDocument *doc, const char *section, const char *key);

#ifdef __cplusplus
}
#endif

#endif // ENTRY_H
