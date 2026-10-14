#define _GNU_SOURCE
#include "yk/entry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Helper: Trim leading and trailing whitespace in-place
static char *trim_whitespace(char *str) {
    char *end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// Free all memory associated with the document
void free_entry_document(EntryDocument *doc) {
    if (!doc) return;
    EntrySection *sec = doc->sections;
    while (sec) {
        EntrySection *next_sec = sec->next;
        EntryNode *node = sec->entries;
        while (node) {
            EntryNode *next_node = node->next;
            free(node->key);
            free(node->raw_value);
            if (node->is_multi) {
                free(node->multi.value);
                free(node->multi.tag);
            }
            free(node);
            node = next_node;
        }
        free(sec->name);
        free(sec);
        sec = next_sec;
    }
    free(doc);
}

// Internal stream parser shared by both files and strings
static EntryDocument *parse_entry_stream(FILE *file) {
    if (!file) return NULL;

    EntryDocument *doc = malloc(sizeof(EntryDocument));
    if (!doc) return NULL;
    doc->sections = NULL;
    
    // Default global section for keys appearing before any [SectionHeader]
    EntrySection *current_section = malloc(sizeof(EntrySection));
    current_section->name = strdup("global");
    current_section->entries = NULL;
    current_section->next = NULL;
    doc->sections = current_section;

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char *trimmed = trim_whitespace(line);

        // Skip comment lines and empty lines
        if (trimmed[0] == '#' || trimmed[0] == ';' || trimmed[0] == '\0') {
            continue;
        }

        // Parse Section Header: [SectionName]
        if (trimmed[0] == '[' && trimmed[strlen(trimmed) - 1] == ']') {
            trimmed[strlen(trimmed) - 1] = '\0';
            char *sec_name = trim_whitespace(trimmed + 1);

            EntrySection *new_sec = malloc(sizeof(EntrySection));
            new_sec->name = strdup(sec_name);
            new_sec->entries = NULL;
            new_sec->next = NULL;

            // Append section to list
            EntrySection *iter = doc->sections;
            while (iter->next) iter = iter->next;
            iter->next = new_sec;
            current_section = new_sec;
            continue;
        }

        // Parse Key = Value
        char *eq = strchr(trimmed, '=');
        if (eq) {
            *eq = '\0';
            char *key = trim_whitespace(trimmed);
            char *val = trim_whitespace(eq + 1);

            EntryNode *node = malloc(sizeof(EntryNode));
            node->key = strdup(key);
            node->raw_value = strdup(val);
            node->next = NULL;

            // Check for multi-value tagged syntax (e.g., textbook:textbook_xpm)
            char *colon = strchr(val, ':');
            if (colon) {
                *colon = '\0';
                node->is_multi = 1;
                node->multi.value = strdup(trim_whitespace(val));
                node->multi.tag = strdup(trim_whitespace(colon + 1));
            } else {
                node->is_multi = 0;
                node->multi.value = NULL;
                node->multi.tag = NULL;
            }

            // Append node to current section's entry list
            if (!current_section->entries) {
                current_section->entries = node;
            } else {
                EntryNode *n_iter = current_section->entries;
                while (n_iter->next) n_iter = n_iter->next;
                n_iter->next = node;
            }
        }
    }

    return doc;
}

// Parse any standard entry/INI file
EntryDocument *parse_entry_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        return NULL;
    }

    EntryDocument *doc = parse_entry_stream(file);
    fclose(file);
    return doc;
}

// Parse entry data directly from a memory string using fmemopen
EntryDocument *parse_entry_string(const char *content) {
    if (!content) return NULL;

    FILE *file = fmemopen((void *)content, strlen(content), "r");
    if (!file) return NULL;

    EntryDocument *doc = parse_entry_stream(file);
    fclose(file);
    return doc;
}

// --- Query Helpers ---

static EntryNode *find_node(EntryDocument *doc, const char *section, const char *key) {
    if (!doc) return NULL;
    EntrySection *sec = doc->sections;
    while (sec) {
        if (strcmp(sec->name, section) == 0) {
            EntryNode *node = sec->entries;
            while (node) {
                if (strcmp(node->key, key) == 0) return node;
                node = node->next;
            }
        }
        sec = sec->next;
    }
    return NULL;
}

const char *get_entry_string(EntryDocument *doc, const char *section, const char *key, const char *default_val) {
    EntryNode *node = find_node(doc, section, key);
    if (!node) return default_val;
    return node->is_multi ? node->multi.value : node->raw_value;
}

int get_entry_int(EntryDocument *doc, const char *section, const char *key, int default_val) {
    const char *str = get_entry_string(doc, section, key, NULL);
    if (!str) return default_val;
    return atoi(str);
}

const EntryMultiValue *get_entry_multi(EntryDocument *doc, const char *section, const char *key) {
    EntryNode *node = find_node(doc, section, key);
    if (node && node->is_multi) {
        return &node->multi;
    }
    return NULL;
}
