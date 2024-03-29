/// @file rm.c
/// @brief Remove files
/// @copyright (c) 2014-2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <fcntl.h>
#include <strerror.h>
#include <stdbool.h>
#include <libgen.h>

static void remove_all_direntries(const char* path)
{
    char directory[PATH_MAX], fullpath[PATH_MAX];
    int fd;

    if (strcmp(path, "*") == 0) {
        getcwd(directory, PATH_MAX);
    } else {
        // Get the parent directory.
        if (!dirname(path, directory, sizeof(directory))) {
            errx(EXIT_FAILURE, "rm: cannot remove '%s': File name too long", path);
        }
    }

    if ((fd = open(directory, O_RDONLY | O_DIRECTORY, 0)) != -1) {
        dirent_t dent;
        while (getdents(fd, &dent, sizeof(dirent_t)) == sizeof(dirent_t)) {
            strcpy(fullpath, directory);
            strcat(fullpath, dent.d_name);
            if (dent.d_type == DT_REG) {
                if (unlink(fullpath) == 0) {
                    if (lseek(fd, -1, SEEK_CUR) != -1) {
                        printf("Failed to move back the getdents...\n");
                    }
                }
            }
        }
        close(fd);
    }
}

int main(int argc, char **argv)
{
    if (argc <= 1) {
        printf("%s: missing operand.\n", argv[0]);
        printf("Try '%s --help' for more information.\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "--help") == 0) {
        printf("Remove (unlink) the FILE(s).\n");
        printf("Usage:\n");
        printf("    rm <filename>...\n");
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        char *filename = argv[i];
        if (strcmp(basename(filename), "*") == 0) {
            remove_all_direntries(filename);
        } else {
            if (unlink(filename) < 0) {
                err(EXIT_FAILURE, "%s: cannot remove '%s'", argv[0], filename);
            }
        }
    }
    return 0;
}
