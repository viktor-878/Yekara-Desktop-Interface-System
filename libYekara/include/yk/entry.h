#ifndef YK_ENTRY_H
#define YK_ENTRY_H

/*
 * YeKara entry file format:
 *
 *   [Section Name]
 *   Key=value1:value2:value3;
 *
 * - Every entry ends with ';' (anything after the first ';' is ignored)
 * - Values are separated by ':' and may be any number (one or more)
 * - Whitespace around keys and values is trimmed
 * - Lines starting with '#' and blank lines are ignored
 * - Entries before any [Section] go into the "global" section
 */

typedef struct EntryNode {
    char *key;
    char **values;          /* array of value_count strings */
    int value_count;
    struct EntryNode *next;
} EntryNode;

typedef struct EntrySection {
    char *name;
    EntryNode *entries;
    struct EntrySection *next;
} EntrySection;

typedef struct EntryDocument {
    EntrySection *sections;
} EntryDocument;

EntryDocument *parse_entry_file(const char *filename);
EntryDocument *parse_entry_string(const char *content);
void free_entry_document(EntryDocument *doc);

/* Number of values for a key (0 if the key doesn't exist) */
int get_entry_count(EntryDocument *doc, const char *section, const char *key);

/* Value at index, or default_val if missing / out of range */
const char *get_entry_value(EntryDocument *doc, const char *section,
                            const char *key, int index, const char *default_val);

/* First value of a key (shortcut for index 0) */
const char *get_entry_string(EntryDocument *doc, const char *section,
                             const char *key, const char *default_val);

int get_entry_int(EntryDocument *doc, const char *section,
                  const char *key, int default_val);

/* true/yes/on/1 -> 1, false/no/off/0 -> 0, otherwise default_val */
int get_entry_bool(EntryDocument *doc, const char *section,
                   const char *key, int default_val);

/* Pointer to the full value array (NULL if missing); count written to *count */
const char *const *get_entry_values(EntryDocument *doc, const char *section,
                                    const char *key, int *count);

#endif