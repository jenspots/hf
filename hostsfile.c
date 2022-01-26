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
#include <arpa/inet.h>
#include <sys/param.h>

/* ANSI colors based on https://stackoverflow.com/a/3219471/13197584. */
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

/* Memory management parameters. */
#define INITIAL_ARRAY_SIZE 16

/* Various status codes used throughout. */
#define SUCCESS 0
#define FILE_NOT_FOUND 1
#define LOGIC_ERROR 2
#define INVALID_ARGUMENTS 3
#define NON_EXHAUSTIVE_CASE 4
#define MEM_ALLOCATION_FAILURE 5
#define EXISTING_HOSTS_FILE_INVALID 6

/* We're not too concerned about the correctness of the hosts file just yet. */
#define REGEX_HOST_FILE_ENTRY "^([^\t \n]+)[\t ]+([^\t \n]+)\n?$"
#define REGEX_IPv4_PORT "^([0-9.]*):[0-9]+$"
#define REGEX_IPv6_PORT "^\\[(.*)\\]:[0-9]+$"

/* TODO: This should be dynamically set. */
#define HOSTS_FILE_PATH "/etc/hosts"

/* Keeps track of what's inside an element union. */
#define UNION_EMPTY 0
#define UNION_ELEMENT 1
#define UNION_COMMENT 2

/* Keeps track of the IP protocol version. */
enum ip_kind {
    IP_KIND_IPv4,
    IP_KIND_IPv6,
};

/* Simple abstraction of a hosts file. Essentially a vector. */
struct hosts_file {
    struct wrapper * array;
    unsigned int size;
    unsigned int index;
};

/* IP-address and domain name combination. */
struct mapping {
    char* ip;
    enum ip_kind kind;
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
 * Checks whether or not an IP address is IPv4 or IPv6.
 * @param ip A pointer to the IP address.
 * @return An instance of the ip_kind enum.
 */
enum ip_kind parse_ip_address(char * ip)
{
    regex_t regex_ipv4, regex_ipv6;
    regmatch_t capture_groups[2];
    long long ip_start, ip_end;
    unsigned char buffer[MAX(sizeof(struct in_addr), sizeof(struct in6_addr))];
    char * tmp = ip;

    if (regcomp(&regex_ipv4, REGEX_IPv4_PORT, REG_EXTENDED)) {
        exit(LOGIC_ERROR);
    }

    if (regcomp(&regex_ipv6, REGEX_IPv6_PORT, REG_EXTENDED)) {
        exit(LOGIC_ERROR);
    }

    /* If a port is given, retrieve the IP address. */
    if (regexec(&regex_ipv4, ip, 2, capture_groups, 0) == 0) {
        ip_start = capture_groups[1].rm_so;
        ip_end = capture_groups[1].rm_eo;
        tmp = calloc(sizeof(char), ip_end - ip_start + 1);
        strncpy(tmp, &ip[ip_start], ip_end - ip_start);
    } else if (regexec(&regex_ipv6, ip, 2, capture_groups, 0) == 0) {
        ip_start = capture_groups[1].rm_so;
        ip_end = capture_groups[1].rm_eo;
        tmp = calloc(sizeof(char), ip_end - ip_start + 1);
        strncpy(tmp, &ip[ip_start], ip_end - ip_start);
    }

    /* Check validity using built-in library. */
    if (inet_pton(AF_INET, tmp, &buffer)) {
        return IP_KIND_IPv4;
    } else if (inet_pton(AF_INET6, tmp, &buffer)) {
        return IP_KIND_IPv6;
    } else {
        fprintf(stderr, ANSI_COLOR_RED "ERROR: %s is not a valid IP address.\n" ANSI_COLOR_RESET, tmp);
        exit(EXISTING_HOSTS_FILE_INVALID);
    }
}

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

            mapping.kind = parse_ip_address(mapping.ip);

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
    fclose(file);
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
                printf("IPv%d address: %s\nDomain: %s\n\n", e.mapping.kind == IP_KIND_IPv4 ? 4 : 6, e.mapping.ip, e.mapping.domain);
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
    enum ip_kind kind;

    if (domain == NULL || ip == NULL) {
        exit(LOGIC_ERROR);
    }

    kind = parse_ip_address(ip);

    /* OPTION A: An existing record will be overwritten. */
    for (int i = 0; i < f.index; ++i) {
        if (f.array[i].type == UNION_ELEMENT) {
            if (f.array[i].element.mapping.kind == kind) {
                if (strcmp(domain, f.array[i].element.mapping.domain) == 0) {
                    free(f.array[i].element.mapping.ip);
                    free(domain);
                    f.array[i].element.mapping.ip = ip;
                    return;
                }
            }
        }
    }

    /* OPTION B: A new record is given. */
    hosts_file_grow(f);
    f.array[f.index].type = UNION_ELEMENT;
    f.array[f.index].element.mapping.ip = ip;
    f.array[f.index].element.mapping.domain = domain;
    f.array[f.index].element.mapping.kind = kind;
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
 * Write a hosts file to the file at a given path.
 * @param pathname The path of the file to write to.
 * @param hosts_file The host file to be written.
 */
void hosts_file_write(char * pathname, struct hosts_file hosts_file)
{
    FILE * file = fopen(pathname, "w");

    if (file) {
        hosts_file_export(file, hosts_file);
    } else {
        exit(FILE_NOT_FOUND);
    }

    fclose(file);
}

/**
 * Prints the help page and exits the program with an error.
 */
void print_help_and_exit()
{
    printf("HOSTFILE: command line interface for editing hosts files easily.\n");
    printf("Copyright (c) by Jens Pots\n");
    printf("Licensed under AGPL 3.0-only\n");
    printf("\n");
    printf("IMPORTANT\n");
    printf("\tWriting to /etc/hosts requires root privileges.\n");
    printf("OPTIONS\n");
    printf("\t-a <domain> <ip>\tAdd an entry.\n");
    printf("\t-e\t\t\tEcho the current hosts file.\n");
    printf("\t-l\t\t\tList all entries.\n");
    printf("\t-r <domain>\t\tRemove an entry.\n");
    exit(INVALID_ARGUMENTS);
}

int main(int argc, char ** argv)
{
    /* TODO: The following statements are toy-examples for testing purposes. */

    struct hosts_file hosts_file = hosts_file_init(HOSTS_FILE_PATH);

    if (argc == 1) {
        print_help_and_exit();
    }

    if (strcmp(argv[1], (const char *) &"-l") == 0) {
        hosts_file_print(hosts_file);
    }

    if (strcmp(argv[1], (const char *) &"-r") == 0) {
        hosts_file_remove(hosts_file, strdup(argv[2]));
        hosts_file_write(HOSTS_FILE_PATH, hosts_file);
    }

    if (strcmp(argv[1], (const char *) &"-e") == 0) {
        hosts_file_export(stdout, hosts_file);
    }

    if (strcmp(argv[1], (const char *) &"-a") == 0) {
        hosts_file_add(hosts_file, strdup(argv[3]), strdup(argv[2]));
        hosts_file_write(HOSTS_FILE_PATH, hosts_file);
    }

    hosts_file_free(hosts_file);

    return SUCCESS;
}
