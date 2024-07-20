///// @file doas.c
/// @brief Execute commands as different users
/// @copyright (c) 2024 Florian Fischer
/// This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <err.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <readpasswd.h>
#include <shadow.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strerror.h>
#include <string.h>
#include <sys/unistd.h>
#include <termios.h>

/// Maximum length of credentials.
#define CREDENTIALS_LENGTH 50

#define DOAS_CONFIG "/etc/doas.conf"

static inline void __print_lecture(void)
{
    printf(
"We trust you have received the usual lecture from the System Administrator.\n"
"It usually boils down to these three things:\n"
"#1) Respect the privacy of others.\n"
"#2) Think before you type.\n"
"#3) With great power comes great responsibility.\n\n"
    );
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

        // ignore lines starting with #
        if (line[0] == '#')
            continue;

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

int main(int argc, char **argv)
{
    if (argc == 1) {
        errx(EXIT_FAILURE, "Usage: %s <command>", argv[0]);
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Execute commands as another user\n");
            printf("Usage:\n");
            printf("    %s <command>\n", argv[0]);
            return EXIT_SUCCESS;
        }
    }

    passwd_t *pwd;
    char password[CREDENTIALS_LENGTH];
    // Check if we can find the user.
    if ((pwd = getpwuid(getuid())) == NULL) {
        if (errno == ENOENT) {
            errx(EXIT_FAILURE, "The current user is not in the passwd file.");
        } else if (errno == 0) {
            errx(EXIT_FAILURE, "Cannot access passwd file.");
        } else {
            err(EXIT_FAILURE, "Unknown error");
        }
    }

    if (__check_permission(argc - 1, &argv[1], pwd)) {
        errx(EXIT_FAILURE, "User %s not allowed to use doas\n", pwd->pw_name);
    }

    __print_lecture();

    struct spwd *spwd;
    if ((spwd = getspnam(pwd->pw_name)) == NULL) {
        err(EXIT_FAILURE, "Could not retrieve the secret password of %s", pwd->pw_name);
    }

    bool_t passwd_ok = false;
    for (int i = 0; i < 3; ++i) {
        // Get the password.
        while (!readpasswd("Password: ", password, sizeof(password), 0));

        // Check if the password is correct.
        if (strcmp(spwd->sp_pwdp, password) != 0) {
            printf("Wrong password.\n");
            continue;
        }

        passwd_ok = true;
        break;
    }

    if (!passwd_ok) {
        errx(EXIT_FAILURE, "Failed to identify as %s.\n", pwd->pw_name);
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
