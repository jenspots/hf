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
#include <getopt.h>

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

/* Flags set by CLI arguments. */
static int verbose_flag = 0;
static int raw_flag = 0;
static int dry_run_flag = 0;

/* Help message. */
#define WHITESPACE "\t"
static char* help_message =
        "HOSTFILE: command line interface for editing hosts files easily.\n"
        "Copyright (c) by Jens Pots\n"
        "Licensed under AGPL-3.0-only\n"
        "\n"
        "IMPORTANT\n"
        WHITESPACE"Writing to /etc/hosts requires root privileges.\n"
        "FLAGS\n"
        WHITESPACE"--verbose\t\tTurn up verbosity.\n"
        WHITESPACE"--raw\t\t\tDon't humanize output.\n"
        WHITESPACE"--dry-run\t\tSend changes to stdout.\n"
        "OPTIONS\n"
        WHITESPACE"-l --list\t\tList all current entries.\n"
        WHITESPACE"-r --remove <domain>\tRemove an entry.\n";

/* Keeps track of the IP protocol version. */
enum ip_kind {
    IP_KIND_IPv4,
    IP_KIND_IPv6,
};

/* Wraps the union in a struct to keep track of its type. */
struct hosts_file_entry {
    int type;
    union value {
        struct map {
            enum ip_kind kind;
            char* ip;
            char* domain;
        } map;
        char * comment;
    } value;
};

/* Simple abstraction of a hosts file. Essentially a vector. */
struct hosts_file {
    struct hosts_file_entry * entries;
    unsigned int size;
    unsigned int index;
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
 * @param hosts_file The hosts file struct that will be grown.
 */
void hosts_file_grow(struct hosts_file hosts_file)
{
    if (hosts_file.index == hosts_file.size) {
        hosts_file.size *= 2;
        hosts_file.entries = realloc(hosts_file.entries, sizeof(struct map) * hosts_file.size);
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
    struct hosts_file hosts_file;
    struct hosts_file_entry entry;

    /* Compiles regular expression. */
    if (regcomp(&regex, REGEX_HOST_FILE_ENTRY, REG_EXTENDED)) {
        exit(LOGIC_ERROR);
    }

    /* Open file. */
    if (!(file = fopen(pathname, "r"))) {
        exit(FILE_NOT_FOUND);
    }

    /* Initialize array. */
    hosts_file.size = INITIAL_ARRAY_SIZE;
    hosts_file.entries = calloc(sizeof(struct hosts_file_entry), INITIAL_ARRAY_SIZE);

    /* Read file line-by-line. */
    for (index = 0; getline(&line, &length, file) != -1; ++index) {
        if (line[0] != '#' && regexec(&regex, line, 3, capture_groups, 0) == 0) {
            dom_start = capture_groups[2].rm_so;
            dom_end   = capture_groups[2].rm_eo;
            ip_start  = capture_groups[1].rm_so;
            ip_end    = capture_groups[1].rm_eo;
            entry.type = UNION_ELEMENT;
            entry.value.map.ip = calloc(sizeof(char), ip_end - ip_start + 1);
            entry.value.map.domain = calloc(sizeof(char), dom_end - dom_start + 1);
            strncpy(entry.value.map.ip, &line[ip_start], ip_end - ip_start);
            strncpy(entry.value.map.domain, &line[dom_start], dom_end - dom_start);
            entry.value.map.kind = parse_ip_address(entry.value.map.ip);
            free(line);
        } else {
            /* We don't free the read line since we keep it as a comment! */
            entry.type = UNION_COMMENT;
            entry.value.comment = line;
        }

        hosts_file_grow(hosts_file);
        hosts_file.entries[index] = entry;
        line = NULL;
    }

    fclose(file);
    hosts_file.index = index;

    return hosts_file;
}

/**
 * Frees a hosts_file struct and it's elements from memory.
 * @param hosts_file The struct to be deleted.
 */
void hosts_file_free(struct hosts_file hosts_file)
{
    struct hosts_file_entry entry;

    for (int i = 0; i < hosts_file.size; ++i) {
        entry = hosts_file.entries[i];
        switch (entry.type) {
            case UNION_EMPTY:
                break;
            case UNION_ELEMENT:
                free(entry.value.map.ip);
                free(entry.value.map.domain);
                break;
            case UNION_COMMENT:
                free(entry.value.comment);
                break;
            default:
                exit(NON_EXHAUSTIVE_CASE);
        }
    }

    free(hosts_file.entries);
}

/**
 * Prints out a hosts_file struct to the console.
 * @param hosts_file The struct to be printed.
 */
void hosts_file_print(struct hosts_file hosts_file)
{
    struct hosts_file_entry entry;
    int first_print = 1;

    for (int i = 0; i < hosts_file.size; ++i) {
        entry = hosts_file.entries[i];
        if (entry.type == UNION_ELEMENT) {
            first_print ? first_print = 0 : printf("\n");
            printf("Address: %s\n", entry.value.map.ip);
            printf("Domain: %s\n", entry.value.map.domain);
            if (verbose_flag) {
                printf("Kind: IPv%d\n", entry.value.map.kind == IP_KIND_IPv4 ? 4 : 6);
                printf("Line: %d\n", i);
            }
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
        if (f.entries[i].type == UNION_ELEMENT) {
            if (f.entries[i].value.map.kind == kind) {
                if (strcmp(domain, f.entries[i].value.map.domain) == 0) {
                    free(f.entries[i].value.map.ip);
                    free(domain);
                    f.entries[i].value.map.ip = ip;
                    return;
                }
            }
        }
    }

    /* OPTION B: A new record is given. */
    hosts_file_grow(f);
    f.entries[f.index].type = UNION_ELEMENT;
    f.entries[f.index].value.map.ip = ip;
    f.entries[f.index].value.map.domain = domain;
    f.entries[f.index].value.map.kind = kind;
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
        if (f.entries[i].type == UNION_ELEMENT) {
            if (strcmp(domain, f.entries[i].value.map.domain) == 0) {
                free(f.entries[i].value.map.ip);
                free(f.entries[i].value.map.domain);
                f.entries[i].type = UNION_EMPTY;
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
    struct hosts_file_entry entry;

    for (int i = 0; i < hosts_file.size; ++i) {
        entry = hosts_file.entries[i];
        switch (entry.type) {
            case UNION_EMPTY:
                break;
            case UNION_ELEMENT:
                fprintf(f, "%s\t%s\n", entry.value.map.ip, entry.value.map.domain);
                break;
            case UNION_COMMENT:
                fprintf(f, "%s", entry.value.comment);
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
    if (!dry_run_flag) {
        FILE *file = fopen(pathname, "w");

        if (file) {
            hosts_file_export(file, hosts_file);
        } else {
            exit(FILE_NOT_FOUND);
        }

        fclose(file);
    } else {
        hosts_file_export(stdout, hosts_file);
    }
}

int main (int argc, char **argv)
{
    int c;
    struct hosts_file hosts_file = hosts_file_init(HOSTS_FILE_PATH);

    /* Flags + parameters available. */
    char options[] = "hlr:";
    struct option long_options[] = {
            {"verbose", no_argument,       &verbose_flag, 1 },
            {"brief",   no_argument,       &verbose_flag, 0 },
            {"raw",     no_argument,       &raw_flag,     1 },
            {"human",   no_argument,       &raw_flag,     0 },
            {"dry-run", no_argument,       &dry_run_flag, 1 },
            {"list",    no_argument,       NULL, 'l'},
            {"help",    no_argument,       NULL, 'h'},
            {"remove",  required_argument, NULL, 'r'},
            {"version", no_argument,       NULL, 'V'},
            {NULL,      0,                 NULL, 0  }
    };

    /* First, we check for any set flags. */
    while (1) {
        c = getopt_long(argc, argv, options, long_options, NULL);
        if (c == -1) {
            break;
        } else if (c == '?') {
            exit(INVALID_ARGUMENTS);
        }
    };

    /* Reset the getopt_long function internally. */
    optind = 1;

    /* Secondly, we go over the arguments. */
    while (1) {
        switch (getopt_long(argc, argv, options, long_options, NULL)) {
            case -1:
                /* Success! The program may exit. */
                hosts_file_free(hosts_file);
                exit(SUCCESS);

            case 0:
                break;

            case 'l':
                if (raw_flag)
                    hosts_file_export(stdout, hosts_file);
                else
                    hosts_file_print(hosts_file);
                break;

            case 'r':
                hosts_file_remove(hosts_file, strdup(optarg));
                if (dry_run_flag) {
                    if (raw_flag)
                        hosts_file_export(stdout, hosts_file);
                    else
                        hosts_file_print(hosts_file);
                } else {
                    hosts_file_write(HOSTS_FILE_PATH, hosts_file);
                }
                break;

            case 'V':
                printf("Version %s\n", "0.0.1");
                exit(SUCCESS);

            case 'h':
                printf("%s", help_message);
                exit(SUCCESS);

            default:
                break;
        }
    }
}
