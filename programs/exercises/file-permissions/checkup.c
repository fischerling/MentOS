/// @file file-permissions/setup.c
/// @brief Setup the file-permissions exercise
/// @copyright (c) 2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <termios.h>

static inline void __set_io_flags(unsigned flag, bool_t active)
{
    struct termios _termios;
    tcgetattr(STDIN_FILENO, &_termios);
    if (active)
        _termios.c_lflag |= flag;
    else
        _termios.c_lflag &= ~flag;
    tcsetattr(STDIN_FILENO, 0, &_termios);
}

static inline bool_t __get_input(char *input, size_t max_len)
{
    size_t index = 0;
    int c;
    bool_t result = false;

    __set_io_flags(ICANON, false);

    memset(input, 0, max_len);
    do {
        c = getchar();
        // Return Key
        if (c == '\n') {
            input[index] = 0;
            result       = true;
            break;
        } else if (c == '\033') {
            c = getchar();
            if (c == '[') {
                getchar(); // Get the char, and ignore it.
            } else if (c == '^') {
                c = getchar(); // Get the char.
                if (c == 'C') {
                    // However, the ISR of the keyboard has already put the char.
                    // Thus, delete it by using backspace.
                    putchar('\b');
                    putchar('\b');
                    putchar('\n');
                    result = false;
                    break;
                } else if (c == 'U') {
                    // However, the ISR of the keyboard has already put the char.
                    // Thus, delete it by using backspace.
                    putchar('\b');
                    putchar('\b');
                    // Clear the current command.
                    for (size_t it = 0; it < index; ++it) {
                        putchar('\b');
                    }
                    index = 0;
                }
            }
        } else if (c == '\b') {
            if (index > 0) {
                putchar('\b');
                --index;
            }
        } else if (c == 0) {
            // Do nothing.
        } else {
            input[index++] = c;
            if (index == (max_len - 1)) {
                input[index] = 0;
                result       = true;
                break;
            }
        }
    } while (index < max_len);

    __set_io_flags(ICANON, true);

    return result;
}

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

    printf("Wie lautet das erste Wort von Bobs Geheimnis?\n>");
    char answer[33];
    __get_input(answer, sizeof(answer) - 1);
    answer[32] = 0;
    if (strcmp(answer, "Arg") != 0) {
        printf("Das erste Wort lautet leider nicht '%s'.\n", answer);
        exit(EXIT_FAILURE);
    }

    printf("Wie lautet Bobs 'top secret' Geheimnis?\n>");
    __get_input(answer, sizeof(answer) - 1);
    answer[32] = 0;
    if (strcmp(answer, "1337") != 0) {
        printf("Das Geheimnis ist leider nicht '%s'.\n", answer);
        exit(EXIT_FAILURE);
    }

    printf("Gute Arbeit :)\n");
    return 0;
}
