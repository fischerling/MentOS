/// @file fgrep.c
/// @brief `fgrep` program.
/// @copyright (c) 2014-2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include "stddef.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>
#include <strerror.h>
#include <sys/stat.h>

#define FGREP_EXIT_NOT_FOUND (1)
#define FGREP_EXIT_FAILURE (2)

#define FGREP_OUTPUT_LN (1 << 0)
#define FGREP_OUTPUT_FNAME (1 << 1)
#define FGREP_MATCHING_FILES (1 << 2)

struct options {
    const char* fname;
    int before;
    int after;
    int output;
};

/**
 * Output a line according to the output options
 */
static void output(const char* line, int n, const char *fname, int options) {
    if (options & FGREP_OUTPUT_FNAME && options & FGREP_OUTPUT_FNAME)
        printf("%s:%d:%s\n", fname, n, line);
    else if (options & FGREP_OUTPUT_FNAME)
        printf("%s:%s\n", fname, line);
    else if (options & FGREP_OUTPUT_LN)
        printf("%d:%s\n", n, line);
    else
        printf("%s\n", line);
}

static int search(int fd, const char *pattern, const char *fname, struct options *options) {
    int found = 0;
    // Count the searched lines
    int n = 0;
    int after = 0;
    /* int before = 0; */

    size_t pattern_len = strlen(pattern);

    // Prepare the buffer for reading.
    char buffer[BUFSIZ + 1];
    buffer[BUFSIZ] = 0;
    size_t leftover = 0;
    char *line = buffer;

    ssize_t bytes_read = 0;

    while ((bytes_read = read(fd, buffer + leftover, sizeof(buffer) - leftover - 1)) > 0) {
        char *lineend;
        while ((lineend = memchr(line, '\n', sizeof(buffer) - (line - buffer)))) {
            *lineend = 0; // Null-terminate line

            if (after) {
                output(line, n, fname, options->output);
                after--;
            }

            if (strstr(line, pattern) != NULL) {
                found = 1;
                // Only output the filename and be done
                if (options->output & FGREP_MATCHING_FILES) {
                    printf("%s\n", fname);
                    goto done;
                }

                output(line, n, fname, options->output);
                after = options->after;
            }

            line = lineend+1;
            n++;
        }

        leftover = (leftover + bytes_read) - (line - buffer); // Bytes left in the buffer
        memmove(buffer, line, leftover); // Move the leftover to the front of buffer
        memset(line, 0, leftover); // Clear the leftover
        line = buffer;
    }

done:
    close(fd);
    if (bytes_read < 0) {
        fprintf(STDERR_FILENO, "head: %s: %s\n", options->fname, strerror(errno));
        return -errno;
    }

    return found;
}

int main(int argc, char **argv)
{
    int found = 0;

    if (argc < 2) {
        printf("fgrep: missing operand.\n");
        printf("Try 'fgrep --help' for more information.\n");
        return FGREP_EXIT_FAILURE;
    }

    char **__argv = argv+1;
    int __argc = argc - 1;
    struct options options;

    // Check if `--help` is provided.
    for (int i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-h") == 0)) {
            printf("Search for PATTTERN in each FILE.\n");
            printf("Usage:\n");
            printf("    fgrep PATTERN [FILE]...\n");
            return 0;
        }
        else if (strcmp(argv[i], "-n") == 0) {
            options.output |= FGREP_OUTPUT_LN;
            __argv = argv+i+1;
            __argc--;
        }
        else if (strcmp(argv[i], "-l") == 0) {
            options.output |= FGREP_MATCHING_FILES;
            __argv = argv+i+1;
            __argc--;
        }
        else if (strcmp(argv[i], "-H") == 0) {
            options.output |= FGREP_OUTPUT_FNAME;
            __argv = argv+i+1;
            __argc--;
        }
        else if (strcmp(argv[i], "-A") == 0) {
            //TODO: add error handling
            options.after = atoi(argv[i+1]);
            __argv = argv+i+2;
            __argc -= 2;
            i++;
        }
        else if (strcmp(argv[i], "-B") == 0) {
            //TODO: add error handling
            options.before = atoi(argv[i+1]);
            __argv = argv+i+2;
            __argc -= 2;
            i++;
        }
        else if (strcmp(argv[i], "-C") == 0) {
            //TODO: add error handling
            options.before = options.after = atoi(argv[i+1]);
            __argv = argv+i+2;
            __argc -= 2;
            i++;
        }
    }


    const char* pattern = *__argv;
    if (__argc == 1) {
        found = search(STDIN_FILENO, pattern, "stdin", &options);
        goto exit;
    }

    if (__argc > 3) {
        options.output |= FGREP_OUTPUT_FNAME;
    }

    for (int i = 1; i < __argc; ++i) {
        // Initialize the file path.
        char *fname = __argv[i];

        int fd = open(fname, O_RDONLY, 0);
        if (fd < 0) {
            printf("fgrep: %s: %s\n", fname, strerror(errno));
            found = -errno;
            continue;
        }
        found = found || search(fd, pattern, fname, &options);
    }

exit:
    if (found < 0)
        return FGREP_EXIT_FAILURE;
    return found == 1 ? EXIT_SUCCESS : FGREP_EXIT_NOT_FOUND;
}
