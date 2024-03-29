/// @file file-permissions/setup.c
/// @brief Setup the file-permissions exercise
/// @copyright (c) 2024 Florian Fischer
/// This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <err.h>
#include <fcntl.h>
#include <readline.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <termios.h>

int main(int argc, char **argv)
{
    if (geteuid() != 0) {
        errx(EXIT_FAILURE, "not running as root");
    }

    setreuid(0, 1001);
    int fd = open("/home/alice/secrets.txt", O_RDONLY, 0);
    if (fd > 0) {
        printf("Bob kann noch immer Ihre Geheimnisse lesen!\n"
               "Versuchen Sie die Berechtigungen und den Eigentuemer\n"
               "der Datei secrets.txt zu berichtigen.\n");
        exit(EXIT_FAILURE);
    }

    readline_complete_func = NULL;

    char *answer = readline("Wie lautet das erste Wort von Bobs Geheimnis?\n> ");
    if (strcmp(answer, "Arg") != 0) {
        printf("Das erste Wort lautet leider nicht '%s'.\n", answer);
        exit(EXIT_FAILURE);
    }
    free(answer);

    answer = readline("Wie lautet Bobs 'top secret' Geheimnis?\n> ");
    if (strcmp(answer, "1337") != 0) {
        printf("Das Geheimnis ist leider nicht '%s'.\n", answer);
        exit(EXIT_FAILURE);
    }
    free(answer);

    printf("Gute Arbeit :)\n");
    return 0;
}
