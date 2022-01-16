/*
 * Command line interface to interact with the hosts file.
 *
 * Copyright (C) 2022 Jens Pots.
 * License: AGPL-3.0-only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

/* Memory management parameters. */
#define INITIAL_ARRAY_SIZE 16

/* Various status codes used throughout. */
#define SUCCESS 0
#define FILE_NOT_FOUND 1
#define LOGIC_ERROR 2
#define INVALID_ARGUMENTS 3
#define NON_EXHAUSTIVE_CASE 4
#define MEM_ALLOCATION_FAILURE 5

/* We're not too concerned about the correctness of the hosts file just yet. */
#define REGEX_HOST_FILE_ENTRY "^([^\t \n]+)[\t ]+([^\t \n]+)\n?$"

/* Keeps track of what's inside an element union. */
#define UNION_EMPTY 0
#define UNION_ELEMENT 1
#define UNION_COMMENT 2

/* Simple abstraction of a hosts file. Essentially a vector. */
struct hosts_file {
    struct wrapper * array;
    unsigned int size;
    unsigned int index;
};

/* IP-address and domain name combination. */
struct mapping {
    char* ip;
    char* domain;
};

/* An element in the hosts files is either a mapping or comment. */
union element {
    struct mapping mapping;
    char * comment;
};

/* Wraps the union in a struct to keep track of its type. */
struct wrapper {
    int type;
    union element element;
};

/**
 * Make sure the array of a given hosts file allows for one more element.
 * @param hosts_file The hostsfile struct that will be grown.
 */
void hosts_file_grow(struct hosts_file hosts_file)
{
    if (hosts_file.index == hosts_file.size) {
        hosts_file.size *= 2;
        hosts_file.array = realloc(hosts_file.array, sizeof(struct mapping) * hosts_file.size);
    }
}

/***
 * Parses a file and returns the contents as a hosts_file struct.
 * @param pathname Absolute path of the file to be parsed.
 * @return hosts_file instance.
 * @warning The returned hosts_file_element's array must be freed manually.
 */
struct hosts_file hosts_file_init(char* pathname)
{
    FILE* file;
    int index;
    long long ip_start, ip_end, dom_start, dom_end;
    size_t length;
    char *line = NULL; // Makes `getline` initialize buffer
    regex_t regex;
    regmatch_t capture_groups[3];
    struct hosts_file result;
    struct wrapper wrapper;
    struct mapping mapping;

    /* Compiles regular expression. */
    if (regcomp(&regex, REGEX_HOST_FILE_ENTRY, REG_EXTENDED)) {
        exit(LOGIC_ERROR);
    }

    /* Open file. */
    if (!(file = fopen(pathname, "r"))) {
        exit(FILE_NOT_FOUND);
    }

    /* Initialize array. */
    result.size = INITIAL_ARRAY_SIZE;
    result.array = calloc(sizeof(struct wrapper), INITIAL_ARRAY_SIZE);

    /* Read file line-by-line. */
    for (index = 0; getline(&line, &length, file) != -1; ++index) {
        if (line[0] != '#' && regexec(&regex, line, 3, capture_groups, 0) == 0) {
            dom_start = capture_groups[2].rm_so;
            dom_end   = capture_groups[2].rm_eo;
            ip_start  = capture_groups[1].rm_so;
            ip_end    = capture_groups[1].rm_eo;

            wrapper.type   = UNION_ELEMENT;
            mapping.ip     = calloc(sizeof(char), ip_end  - ip_start  + 1);
            mapping.domain = calloc(sizeof(char), dom_end - dom_start + 1);
            strncpy(mapping.ip,     &line[ip_start],  ip_end  - ip_start);
            strncpy(mapping.domain, &line[dom_start], dom_end - dom_start);

            wrapper.element.mapping = mapping;
            free(line);
        } else {
            wrapper.type = UNION_COMMENT;
            wrapper.element.comment = line;
        }

        hosts_file_grow(result);
        result.array[index] = wrapper;
        line = NULL;
    }

    /* This value must be kept. */
    result.index = index;

    return result;
}

/**
 * Frees a hosts_file struct and it's elements from memory.
 * @param hosts_file The struct to be deleted.
 */
void hosts_file_free(struct hosts_file hosts_file)
{
    struct wrapper w;
    union element e;

    for (int i = 0; i < hosts_file.size; ++i) {
        w = hosts_file.array[i];
        e = w.element;
        switch (w.type) {
            case UNION_EMPTY:
                break;
            case UNION_ELEMENT:
                free(e.mapping.ip);
                free(e.mapping.domain);
                break;
            case UNION_COMMENT:
                free(e.comment);
                break;
            default:
                exit(NON_EXHAUSTIVE_CASE);
        }
    }

    free(hosts_file.array);
}

/**
 * Prints out a hosts_file struct to the console.
 * @param hosts_file The struct to be printed.
 */
void hosts_file_print(struct hosts_file hosts_file)
{
    struct wrapper w;
    union element e;

    for (int i = 0; i < hosts_file.size; ++i) {
        w = hosts_file.array[i];
        e = w.element;
        switch (w.type) {
            case UNION_EMPTY:
                break;
            case UNION_ELEMENT:
                printf("IP address: %s\nDomain: %s\n\n", e.mapping.ip, e.mapping.domain);
                break;
            case UNION_COMMENT:
                break;
            default:
                exit(NON_EXHAUSTIVE_CASE);
        }
    }
}

/**
 * Adds/modifies an entry to/in the hosts file.
 * @param f The hosts file that will be modified.
 * @param ip The IP adres of the new entry.
 * @param domain The domain of the new entry.
 */
void hosts_file_add(struct hosts_file f, char * ip, char * domain)
{
    if (domain == NULL || ip == NULL) {
        exit(LOGIC_ERROR);
    }

    /* OPTION A: An existing record will be overwritten. */
    for (int i = 0; i < f.index; ++i) {
        if (f.array[i].type == UNION_ELEMENT) {
            if (strcmp(domain, f.array[i].element.mapping.domain) == 0) {
                free(f.array[i].element.mapping.ip);
                free(domain);
                f.array[i].element.mapping.ip = ip;
                return;
            }
        }
    }

    /* OPTION B: A new record is given. */
    hosts_file_grow(f);
    f.array[f.index].type = UNION_ELEMENT;
    f.array[f.index].element.mapping.ip = ip;
    f.array[f.index].element.mapping.domain = domain;
    ++(f.index);
}

/**
 * Adds/modifies an entry to/in the hosts file.
 * @param f The hosts file that will be modified.
 * @param ip The IP adres of the new entry.
 * @param domain The domain of the new entry.
 */
void hosts_file_remove(struct hosts_file f, char * domain)
{
    if (domain == NULL) {
        exit(LOGIC_ERROR);
    }

    for (int i = 0; i < f.index; ++i) {
        if (f.array[i].type == UNION_ELEMENT) {
            if (strcmp(domain, f.array[i].element.mapping.domain) == 0) {
                free(f.array[i].element.mapping.ip);
                free(f.array[i].element.mapping.domain);
                f.array[i].type = UNION_EMPTY;
                return;
            }
        }
    }
}

/**
 * Exports a hosts file struct to a file.
 * @param f Target file.
 * @param hosts_file Hosts file that will be written to the file.
 */
void hosts_file_export(FILE* f, struct hosts_file hosts_file)
{
    struct wrapper w;
    union element e;

    for (int i = 0; i < hosts_file.size; ++i) {
        w = hosts_file.array[i];
        e = w.element;
        switch (w.type) {
            case UNION_EMPTY:
                break;
            case UNION_ELEMENT:
                fprintf(f, "%s\t%s\n", e.mapping.ip, e.mapping.domain);
                break;
            case UNION_COMMENT:
                fprintf(f, "%s", e.comment);
                break;
            default:
                exit(NON_EXHAUSTIVE_CASE);
        }
    }
}

/**
 * Prints the help page and exits the program with an error.
 */
void print_help_and_exit()
{
    printf("OPTIONS\n");
    printf("\t-l\t--list\tList all hosts file entries.\n");
    exit(INVALID_ARGUMENTS);
}

int main(int argc, char ** argv)
{
    /* TODO: The following statements are toy-examples for testing purposes. */

    if (argc == 1) {
        print_help_and_exit();
    }

    if (strcmp(argv[1], (const char *) &"-l") == 0) {
        struct hosts_file f = hosts_file_init("/etc/hosts");
        hosts_file_print(f);
        hosts_file_free(f);
    }

    if (strcmp(argv[1], (const char *) &"-r") == 0) {
        struct hosts_file f = hosts_file_init("/etc/hosts");
        hosts_file_remove(f, strdup(argv[2]));
        hosts_file_print(f);
        hosts_file_free(f);
    }

    if (strcmp(argv[1], (const char *) &"--dry-run") == 0) {
        struct hosts_file f = hosts_file_init("/etc/hosts");
        hosts_file_export(stdout, f);
        hosts_file_free(f);
    }

    if (strcmp(argv[1], (const char *) &"-a") == 0) {
        struct hosts_file f = hosts_file_init("/etc/hosts");
        hosts_file_add(f, strdup(argv[2]), strdup(argv[3]));
        hosts_file_print(f);
        hosts_file_free(f);
    }

    return SUCCESS;
}
