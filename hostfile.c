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

/* We're not too concerned about the correctness of the hosts file just yet. */
#define REGEX_HOST_FILE_ENTRY "^([^\t \n]+)[\t ]+([^\t \n]+)\n?$"

/* IP-address and domain name combination. */
struct hosts_file_element {
    char* ip;
    char* domain;
};

/* Simple abstraction of a hosts file. Essentially a vector. */
struct hosts_file {
    struct hosts_file_element * array;
    unsigned int size;
};

/***
 * Parses a file and returns the contents as a hosts_file struct.
 * @param pathname Absolute path of the file to be parsed.
 * @return hosts_file instance.
 * @warning The returned hosts_file_element's array must be freed manually.
 */
struct hosts_file parse_file(char* pathname)
{
    FILE* file;
    size_t length;
    char *line = NULL; // Makes `getline` initialize buffer
    regex_t regex;
    regmatch_t capture_groups[3];
    struct hosts_file result;
    int index;

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
    result.array = calloc(sizeof(struct hosts_file_element), INITIAL_ARRAY_SIZE);

    /* Read file line-by-line. When necessary, grow the array. */
    index = 0;
    while (getline(&line, &length, file) != -1) {
        if (line[0] != '#' && regexec(&regex, line, 3, capture_groups, 0) == 0) {
            if (index == result.size) {
                result.size *= 2;
                result.array = realloc(result.array, sizeof(struct hosts_file_element) * result.size);
            }

            result.array[index].ip     = calloc(sizeof(char), capture_groups[1].rm_eo - capture_groups[1].rm_so + 1);
            result.array[index].domain = calloc(sizeof(char), capture_groups[2].rm_eo - capture_groups[2].rm_so + 1);

            strncpy(result.array[index].ip, &line[capture_groups[1].rm_so], capture_groups[1].rm_eo - capture_groups[1].rm_so);
            strncpy(result.array[index].domain, &line[capture_groups[2].rm_so], capture_groups[2].rm_eo - capture_groups[2].rm_so);

            ++index;
        }

        free(line);
        line = NULL;
    }

    return result;
}

int main()
{
    struct hosts_file f = parse_file("/etc/hosts");
    free(f.array);
    return SUCCESS;
}
