/// @file apropos.c
/// @brief Search in the available manual pages.
/// @copyright (c) 2014-2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strerror.h>
#include <string.h>
#include <sys/dirent.h>
#include <sys/unistd.h>

static const char* MAN_PATH = "/usr/share/man";

int main(int argc, char *argv[]) {
    if (argc != 2) {
            printf("Usage: apropos KEYWORD\n");
            return EXIT_FAILURE;
    }

    int fd = open(MAN_PATH, O_RDONLY | O_DIRECTORY, 0);
    if (fd == -1)
        err(EXIT_FAILURE, "cannot access '%s'", MAN_PATH);

    dirent_t dent;
    while (getdents(fd, &dent, sizeof(dirent_t)) == sizeof(dirent_t)) {
        // Shows only regular files
        if (dent.d_type != DT_REG)
            continue;

        char filepath[PATH_MAX];
        strcpy(filepath, "/usr/share/man/");
        strcat(filepath, dent.d_name);
        char *cmd = (char*)malloc(PATH_MAX + strlen("fgrep -H ") + strlen(filepath));
        if (cmd == NULL)
            err(EXIT_FAILURE, "%s: malloc failed", argv[0]);
        sprintf(cmd, "fgrep -l %s %s 2> /dev/null", argv[1], filepath);
        system(cmd);
        free(cmd);
    }
    close(fd);
    return EXIT_SUCCESS;
}
