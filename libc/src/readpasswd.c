/// @file readpasswd.c
/// @brief Get a passphrase from the user
/// @copyright (c) 2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <readpasswd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <termios.h>

char* readpasswd(const char *prompt, char *buf, size_t bufsiz, int flags)
{
    if (bufsiz == 0) {
        errno = EINVAL;
        return NULL;
    }

    if (prompt) {
        printf("%s", prompt);
    }

    size_t index = 0;
    int c;
    char* result = NULL;

    struct termios termios, restore;
    tcgetattr(STDIN_FILENO, &restore);
    memcpy(&termios, &restore, sizeof(restore));

    termios.c_lflag |= ICANON;
    if (flags & RPWD_ECHO_ON) {
        termios.c_lflag |= ECHO;
    } else {
        termios.c_lflag &= ~ECHO;
    }

    tcsetattr(STDIN_FILENO, 0, &termios);

    memset(buf, 0, bufsiz);
    do {
        c = getchar();
        // Return Key
        if (c == '\n') {
            buf[index] = 0;
            result = buf;
            break;
        } else if (c == '\033') {
            c = getchar();
            if (c == '[') {
                c = getchar(); // Get the char, and ignore it.
            } else if (c == '^') {
                c = getchar(); // Get the char.
                if (c == 'C') {
                    break;
                } else if (c == 'U') {
                    index = 0;
                }
            }
        } else if (c == '\b') {
            if (index > 0) {
                --index;
            }
        } else if (c != 0) {
            // Append the read char
            buf[index++] = c;
            // Null-terminate the buffer and return
            if (index == (bufsiz - 1)) {
                buf[index] = 0;
                result = buf;
                break;
            }
        }
    } while (index < bufsiz);

    tcsetattr(STDIN_FILENO, 0, &restore);

    if (!(flags & RPWD_ECHO_ON)) {
        putchar('\n');
    }

    return result;
}


