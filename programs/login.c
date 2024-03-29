///// @file login.c
/// @brief Functions used to manage login.
/// @copyright (c) 2014-2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <string.h>
#include <stdbool.h>
#include <sys/unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <termios.h>
#include <bits/ioctls.h>
#include <pwd.h>
#include <shadow.h>
#include <strerror.h>
#include <stdlib.h>
#include <io/debug.h>
#include <io/ansi_colors.h>
#include <readpasswd.h>

#include <sys/mman.h>
#include <sys/wait.h>

/// Maximum length of credentials.
#define CREDENTIALS_LENGTH 50

static inline int __setup_env(passwd_t *pwd)
{
    // Set the USER.
    if (setenv("USER", pwd->pw_name, 1) == -1) {
        printf("Failed to set env: `USER`\n");
        return 0;
    }
    // Set the SHELL.
    if (setenv("SHELL", pwd->pw_shell, 1) == -1) {
        printf("Failed to set env: `SHELL`\n");
        return 0;
    }
    // Set the HOME.
    if (setenv("HOME", pwd->pw_dir, 1) == -1) {
        printf("Failed to set env: `HOME`\n");
        return 0;
    }
    return 1;
}

static inline void __print_message_file(const char *file)
{
    char buffer[256];
    ssize_t nbytes, total = 0;
    int fd;

    // Try to open the file.
    if ((fd = open(file, O_RDONLY, 0600)) == -1)
        return;
    // Print the file.
    while ((nbytes = read(fd, buffer, sizeof(char) * 256)) > 0) {
        // TODO: Parsing message files for special characters (such as `\t` for time).
        write(STDOUT_FILENO, buffer, nbytes);
        total += nbytes;
    }
    close(fd);
    if (total > 0)
        printf("\n");
}

int main(int argc, char **argv)
{
#if 0
    int *shared = mmap(NULL, sizeof(int), PROT_READ | PROT_WRITE,
                       MAP_SHARED, -1, 0);
    pid_t child;
    int childstate;
    pr_warning("[F] (%p) %d\n", shared, *shared);
    if ((child = fork()) == 0) {
        *shared = 1;
        pr_warning("[C] (%p) %d\n", shared, *shared);
        return 0;
    }
    waitpid(child, &childstate, 0);
    pr_warning("[F] (%p) %d\n", shared, *shared);
    return 0;
    while (1) {}
#endif
    // Print /etc/issue if it exists.
    __print_message_file("/etc/issue");

    passwd_t *pwd;
    char username[CREDENTIALS_LENGTH], password[CREDENTIALS_LENGTH];
    do {
        // Get the username.
        while (!readpasswd("Username: ", username, sizeof(username), RPWD_ECHO_ON));

        while (!readpasswd("Password: ", password, sizeof(password), 0));

        // Check if we can find the user.
        if ((pwd = getpwnam(username)) == NULL) {
            if (errno == ENOENT) {
                printf("The given name was not found.\n");
            } else if (errno == 0) {
                printf("Cannot access passwd file.\n");
            } else {
                printf("Unknown error (%s).\n", strerror(errno));
            }
            continue;
        }

        struct spwd *spwd;
        if ((spwd = getspnam(username)) == NULL) {
            printf("Could not retrieve the secret password of %s:%s\n", username, strerror(errno));
            continue;
        }

        // Check if the password is correct.
        if (strcmp(spwd->sp_pwdp, password) != 0) {
            printf("Wrong password.\n");
            continue;
        }

        break;
    } while (true);

    // If there is not shell set for the user, should we rollback to standard shell?
    if (pwd->pw_shell == NULL) {
        printf("login: There is no shell set for the user `%s`.\n", pwd->pw_name);
        return 1;
    }

    // Set the standard environmental variables.
    if (!__setup_env(pwd)) {
        printf("login: Failed to setup the environmental variables.\n");
        return 1;
    }

    // Set the group id.
    if (setgid(pwd->pw_gid) < 0) {
        printf("login: Failed to change group id: %s\n", strerror(errno));
        return 1;
    }

    // Set the user id.
    if (setuid(pwd->pw_uid) < 0) {
        printf("login: Failed to change user id: %s\n", strerror(errno));
        return 1;
    }

    printf("\n");

    // Print /etc/motd if it exists.
    __print_message_file("/etc/motd");

    // Welcome the user.
    puts(BG_WHITE FG_BLACK);
    printf("Welcome " FG_RED "%s" FG_BLACK "...\n", pwd->pw_name);
    puts(BG_BLACK FG_WHITE_BRIGHT);

    // Call the shell.
    char *_argv[] = { pwd->pw_shell, (char *)NULL };
    if (execv(pwd->pw_shell, _argv) == -1) {
        printf("login: Failed to execute the shell.\n");
        printf("login: %s.\n", strerror(errno));
        return 1;
    }
    return 0;
}
