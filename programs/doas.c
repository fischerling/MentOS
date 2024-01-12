///// @file doas.c
/// @brief Execute commands as different users
/// @copyright (c) 2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <string.h>
#include <stdbool.h>
#include <sys/unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <pwd.h>
#include <grp.h>
#include <shadow.h>
#include <strerror.h>
#include <stdlib.h>

/// Maximum length of credentials.
#define CREDENTIALS_LENGTH 50

#define DOAS_CONFIG "/etc/doas.conf"

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

static inline int __check_identity(char *identity, passwd_t *pwd)
{
    
    char *user, *group;
    struct group *grp;
    if (identity[0] != ':') {
        user = strtok(identity, ":");
        if (strcmp(user, pwd->pw_name) == 0)
            return 0;
    }

    if (identity[0] == ':')
        group = strtok(identity+1, ":");
    else
        group = strtok(NULL, ":");
    if (group == NULL)
        return 1;

    while((grp = getgrent()) != NULL) {
        if (strcmp(grp->gr_name, group))
            continue;

        for (char **gr_mem = grp->gr_mem; gr_mem; gr_mem++) {
            if (strcmp(*gr_mem, pwd->pw_name) == 0)
                return 0;
        }
    }

    return 1;
}

static inline int __check_permission(int argc, char *argv[], passwd_t *pwd)
{
    char line[256];
    int fd;
    int ret = EPERM;

    // Try to open the file.
    if ((fd = open(DOAS_CONFIG, O_RDONLY, 0600)) == -1)
        return ENOENT;

    // Read the lines of the file.
    while (fgets(line, sizeof(line), fd) != NULL) {
        // strip newline
        if (line[strlen(line) -1] == '\n')
            line[strlen(line) -1] = 0;

        char *modifier = strtok(line, " ");
        if (modifier == NULL)
           continue;

        if (strncmp(modifier, "permit", strlen("permit"))) {
           return EINVAL;
        }

        char *identity = strtok(NULL, " ");
        if (identity == NULL) {
           return EINVAL;
        }


        if (__check_identity(identity, pwd) == 0) {
            ret = 0;
            goto done;
        }
    }

done:
    close(fd);
    return ret;
}

/// @brief Read a password from stdin
static inline bool_t __get_passwd(char *input, size_t max_len)
{
    size_t index = 0;
    int c;
    bool_t result = false;

    __set_io_flags(ICANON, false);
    __set_io_flags(ECHO, false);

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
                c = getchar(); // Get the char, and ignore it.
            } else if (c == '^') {
                c = getchar(); // Get the char.
                if (c == 'C') {
                    // However, the ISR of the keyboard has already put the char.
                    // Thus, delete it by using backspace.
                    result = false;
                    break;
                } else if (c == 'U') {
                    index = 0;
                }
            }
        } else if (c == '\b') {
            if (index > 0) {
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

    __set_io_flags(ECHO, true);
    putchar('\n');
    __set_io_flags(ICANON, true);

    return result;
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        printf("Usage: %s <command>\n", argv[0]);
        exit(1);
    }

    passwd_t *pwd;
    char password[CREDENTIALS_LENGTH];
    // Check if we can find the user.
    if ((pwd = getpwuid(getuid())) == NULL) {
        if (errno == ENOENT) {
            printf("The current user is not in the passwd file.\n");
            exit(1);
        } else if (errno == 0) {
            printf("Cannot access passwd file.\n");
            exit(1);
        } else {
            printf("Unknown error (%s).\n", strerror(errno));
            exit(1);
        }
    }

    if (__check_permission(argc - 1, &argv[1], pwd)) {
        printf("User %s not allowed to use doas\n", pwd->pw_name);
        exit(1);
    }

    struct spwd *spwd;
    if ((spwd = getspnam(pwd->pw_name)) == NULL) {
        printf("Could not retrieve the secret password of %s:%s\n", pwd->pw_name, strerror(errno));
        exit(1);
    }

    bool_t passwd_ok = false;
    for (int i = 0; i < 3; ++i) {
        // Get the password.
        do {
            printf("Password: ");
        } while (!__get_passwd(password, CREDENTIALS_LENGTH));

        // Check if the password is correct.
        if (strcmp(spwd->sp_pwdp, password) != 0) {
            printf("Wrong password.\n");
            continue;
        }

        passwd_ok = true;
        break;
    }

    if (!passwd_ok) {
        printf("Failed to identify as %s.\n", pwd->pw_name);
        exit(1);
    }

    // TODO: Set the user id to an arbitrary user specified in the config
    // setreuid(-1, ?);

    // Call the command.
    if (execvp(argv[1], &argv[1]) == -1) {
        printf("%s: Failed to execute %s.\n", argv[0], argv[1]);
        printf("%s: %s.\n", argv[0], strerror(errno));
        exit(1);
    }
    return 0;
}
