/// @file system.c
/// @brief Implementation of the system library function
/// @copyright (c) 2024 Florian Fischer
/// See LICENSE.md for details.

#include <stdlib.h>
#include <signal.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/wait.h>

int system(const char *cmd)
{
    pid_t pid;
    sigset_t old;
    sigaction_t sa = { .sa_handler = SIG_IGN }, oldint, oldquit;
    int status = -1, ret;

    if (!cmd) return 1;

    // Ignore SIGINT and SIGQUIT
    sigaction(SIGINT, &sa, &oldint);
    sigaction(SIGQUIT, &sa, &oldquit);

    // Block SIGCHLD
    sigaddset(&sa.sa_mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &sa.sa_mask, &old);

    pid = fork();
    if (pid == 0) {
        // Potentially reset the child's SIGINT and SIGQUIT handling
        sigaction_t reset_sa = { .sa_handler = SIG_DFL };
        if (oldint.sa_handler != SIG_IGN) sigaction(SIGINT, &reset_sa, 0);
        if (oldquit.sa_handler != SIG_IGN) sigaction(SIGQUIT, &reset_sa, 0);

        // Restore the child's signal mask
        sigprocmask(SIG_SETMASK, &old, 0);

        char *argv[] = {"shell", "-c", (char *)cmd, 0};
        execv("/bin/shell", argv);
    }

    // Fork failed
    if (pid < 0) {
        return pid;
    }

    // Await the forked child process
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR);

    // Restore the process' signal handling
    sigaction(SIGINT, &oldint, NULL);
    sigaction(SIGQUIT, &oldquit, NULL);
    sigprocmask(SIG_SETMASK, &old, NULL);

    // Return the child's exit status
    return status;
}
