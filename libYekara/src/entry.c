#define _GNU_SOURCE
#include "yk/entry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

/* Trim leading and trailing whitespace in-place */
static char *trim_whitespace(char *str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static void free_node(EntryNode *node) {
    if (!node) return;
    for (int i = 0; i < node->value_count; i++) free(node->values[i]);
    free(node->values);
    free(node->key);
    free(node);
}

void free_entry_document(EntryDocument *doc) {
    if (!doc) return;
    EntrySection *sec = doc->sections;
    while (sec) {
        EntrySection *next_sec = sec->next;
        EntryNode *node = sec->entries;
        while (node) {
            EntryNode *next_node = node->next;
            free_node(node);
            node = next_node;
        }
        free(sec->name);
        free(sec);
        sec = next_sec;
    }
    free(doc);
}

static EntrySection *new_section(const char *name) {
    EntrySection *sec = calloc(1, sizeof(EntrySection));
    if (!sec) return NULL;
    sec->name = strdup(name);
    if (!sec->name) { free(sec); return NULL; }
    return sec;
}
/* Split "a:b:c" into trimmed, heap-allocated values.
   An escaped colon (\:) is treated as a literal colon. */
static char **split_values(char *val, int *count_out) {
    int count = 1;
    int escaped = 0;

    for (char *p = val; *p; p++) {
        if (escaped) {
            escaped = 0;
            continue;
        }

        if (*p == '\\') {
            escaped = 1;
            continue;
        }

        if (*p == ':') count++;
    }

    char **values = calloc(count, sizeof(char *));
    if (!values) return NULL;

    int i = 0;
    char *start = val;
    char *read = val;
    char *write = val;

    for (;;) {
        if (*read == '\\' && read[1] == ':') {
            *write++ = ':';
            read += 2;
        } else if (*read == ':' || *read == '\0') {
            char delimiter = *read;
            *write = '\0';

            values[i] = strdup(trim_whitespace(start));
            if (!values[i]) {
                for (int j = 0; j < i; j++)
                    free(values[j]);
                free(values);
                return NULL;
            }

            i++;

            if (delimiter == '\0') break;

            read++;
            start = write = read;
        } else {
            *write++ = *read++;
        }
    }

    *count_out = i;
    return values;
}

/* Shared stream parser for files and strings */
static EntryDocument *parse_entry_stream(FILE *file) {
    if (!file) return NULL;

    EntryDocument *doc = calloc(1, sizeof(EntryDocument));
    if (!doc) return NULL;

    EntrySection *current = new_section("global");
    if (!current) { free(doc); return NULL; }
    doc->sections = current;
    EntrySection *sec_tail = current;
    EntryNode *node_tail = NULL;

    char *line = NULL;
    size_t cap = 0;

    while (getline(&line, &cap, file) != -1) {   /* no fixed line-length limit */
        char *trimmed = trim_whitespace(line);

        /* Comments and blank lines */
        if (trimmed[0] == '#' || trimmed[0] == '\0') continue;

        /* Section header: [Name] */
        size_t len = strlen(trimmed);
        if (trimmed[0] == '[' && trimmed[len - 1] == ']') {
            trimmed[len - 1] = '\0';
            EntrySection *sec = new_section(trim_whitespace(trimmed + 1));
            if (!sec) goto fail;
            sec_tail->next = sec;
            sec_tail = sec;
            current = sec;
            node_tail = NULL;
            continue;
        }

        /* Key=value1:value2:...; */
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim_whitespace(trimmed);
        char *val = eq + 1;

        /* The first ';' terminates the entry; anything after it is ignored.
           A missing terminator is tolerated. */
        char *semi = strchr(val, ';');
        if (semi) *semi = '\0';

        EntryNode *node = calloc(1, sizeof(EntryNode));
        if (!node) goto fail;
        node->key = strdup(key);
        node->values = node->key ? split_values(val, &node->value_count) : NULL;
        if (!node->values) { free_node(node); goto fail; }

        if (node_tail) node_tail->next = node;
        else current->entries = node;
        node_tail = node;
    }

    free(line);
    return doc;

fail:
    free(line);
    free_entry_document(doc);
    return NULL;
}

EntryDocument *parse_entry_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return NULL;
    EntryDocument *doc = parse_entry_stream(file);
    fclose(file);
    return doc;
}

EntryDocument *parse_entry_string(const char *content) {
    if (!content) return NULL;
    FILE *file = fmemopen((void *)content, strlen(content), "r");
    if (!file) return NULL;
    EntryDocument *doc = parse_entry_stream(file);
    fclose(file);
    return doc;
}

/* --- Query helpers --- */

static EntryNode *find_node(EntryDocument *doc, const char *section, const char *key) {
    if (!doc || !section || !key) return NULL;
    for (EntrySection *sec = doc->sections; sec; sec = sec->next) {
        if (strcmp(sec->name, section) != 0) continue;
        for (EntryNode *node = sec->entries; node; node = node->next)
            if (strcmp(node->key, key) == 0) return node;
    }
    return NULL;
}

int get_entry_count(EntryDocument *doc, const char *section, const char *key) {
    EntryNode *node = find_node(doc, section, key);
    return node ? node->value_count : 0;
}

const char *get_entry_value(EntryDocument *doc, const char *section,
                            const char *key, int index, const char *default_val) {
    EntryNode *node = find_node(doc, section, key);
    if (!node || index < 0 || index >= node->value_count) return default_val;
    return node->values[index];
}

const char *get_entry_string(EntryDocument *doc, const char *section,
                             const char *key, const char *default_val) {
    return get_entry_value(doc, section, key, 0, default_val);
}

int get_entry_int(EntryDocument *doc, const char *section,
                  const char *key, int default_val) {
    const char *str = get_entry_string(doc, section, key, NULL);
    return (str && *str) ? atoi(str) : default_val;
}

int get_entry_bool(EntryDocument *doc, const char *section,
                   const char *key, int default_val) {
    const char *s = get_entry_string(doc, section, key, NULL);
    if (!s) return default_val;
    if (!strcasecmp(s, "true") || !strcasecmp(s, "yes") ||
        !strcasecmp(s, "on")   || !strcmp(s, "1")) return 1;
    if (!strcasecmp(s, "false") || !strcasecmp(s, "no") ||
        !strcasecmp(s, "off")   || !strcmp(s, "0")) return 0;
    return default_val;
}

const char *const *get_entry_values(EntryDocument *doc, const char *section,
                                    const char *key, int *count) {
    EntryNode *node = find_node(doc, section, key);
    if (count) *count = node ? node->value_count : 0;
    return node ? (const char *const *)node->values : NULL;
}
