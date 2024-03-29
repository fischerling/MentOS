/// @file shell.c
/// @brief Implement shell functions.
/// @copyright (c) 2014-2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <err.h>
#include <ctype.h>
#include <fcntl.h>
#include <io/ansi_colors.h>
#include <io/debug.h>
#include <libgen.h>
#include <limits.h>
#include <readline.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strerror.h>
#include <string.h>
#include <sys/bitops.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>

// Required by `export`
#define ENV_NORM 1
#define ENV_BRAK 2
#define ENV_PROT 3

// Store the last command status
static int status = 0;
// Store the last command status as string
static char status_buf[4] = { 0 };

static sigset_t oldmask;

static void __block_sigchld(void)
{
    sigset_t mask;
    //sigmask functions only fail on invalid inputs -> no exception handling needed
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
}

static void __unblock_sigchld(void)
{
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

static inline int __is_separator(char c)
{
    return ((c == '\0') || (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r'));
}

static inline int __count_words(const char *sentence)
{
    int result     = 0;
    bool_t inword  = false;
    const char *it = sentence;
    do {
        if (__is_separator(*it)) {
            if (inword) {
                inword = false;
                result++;
            }
        } else {
            inword = true;
        }
    } while (*it++);
    return result;
}

static inline void __set_echo(bool_t active)
{
    struct termios _termios;
    tcgetattr(STDIN_FILENO, &_termios);
    if (active) {
        _termios.c_lflag |= (ICANON | ECHO);
    } else {
        _termios.c_lflag &= ~(ICANON | ECHO);
    }
    tcsetattr(STDIN_FILENO, 0, &_termios);
}

static inline int __folder_contains(
    const char *folder,
    const char *entry,
    int accepted_type,
    dirent_t *result)
{
    int fd = open(folder, O_RDONLY | O_DIRECTORY, 0);
    if (fd == -1) {
        return 0;
    }
    // Prepare the variables for the search.
    dirent_t dent;
    size_t entry_len = strlen(entry);
    int found        = 0;
    while (getdents(fd, &dent, sizeof(dirent_t)) == sizeof(dirent_t)) {
        if (accepted_type && (accepted_type != dent.d_type)) {
            continue;
        }
        if (strncmp(entry, dent.d_name, entry_len) == 0) {
            *result = dent;
            found   = 1;
            break;
        }
    }
    close(fd);
    return found;
}

static inline int __search_in_path(const char *entry, dirent_t *result)
{
    // Determine the search path.
    char *PATH_VAR = getenv("PATH");
    if (PATH_VAR == NULL)
        PATH_VAR = "/bin:/usr/bin";
    // Copy the path.
    char *path = strdup(PATH_VAR);
    // Iterate through the path entries.
    char *token = strtok(path, ":");
    if (token == NULL) {
        free(path);
        return 0;
    }
    do {
        if (__folder_contains(token, entry, DT_REG, result))
            return 1;
    } while ((token = strtok(NULL, ":")));
    free(path);
    return 0;
}

/// @brief Prints the prompt.
static inline void __prompt_print(void)
{
    // Get the current working directory.
    char CWD[PATH_MAX];
    getcwd(CWD, PATH_MAX);
    // If the current working directory is equal to HOME, show ~.
    char *HOME = getenv("HOME");
    if (HOME != NULL)
        if (strcmp(CWD, HOME) == 0)
            strcpy(CWD, "~\0");
    // Get the user.
    char *USER = getenv("USER");
    if (USER == NULL) {
        USER = "error";
    }
    time_t rawtime = time(NULL);
    tm_t *timeinfo = localtime(&rawtime);
    // Get the hostname.
    char *HOSTNAME;
    utsname_t buffer;
    if (uname(&buffer) < 0) {
        HOSTNAME = "error";
    } else {
        HOSTNAME = buffer.nodename;
    }
    printf(FG_GREEN "%s" FG_WHITE "@" FG_CYAN "%s " FG_BLUE_BRIGHT "[%02d:%02d:%02d]" FG_WHITE " [%s] " FG_RESET "\n-> %% ",
           USER, HOSTNAME, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, CWD);
}

static char *__getenv(const char *var)
{
    if (strlen(var) > 1) {
        return getenv(var);
    }

    if (var[0] == '?') {
        sprintf(status_buf, "%d", status);
        return status_buf;
    }

    // TODO: implement access to argv
    /* int arg = strtol(var, NULL, 10); */
    /* if (arg < argc) { */
    /* return argv[arg]; */
    /* } */

    return NULL;
}

static void ___expand_env(char *str, char *buf, size_t buf_len, size_t str_len, bool_t null_terminate)
{
    // Buffer where we store the name of the variable.
    char buffer[BUFSIZ] = { 0 };
    // Flags used to keep track of the special characters.
    unsigned flags = 0;
    // We keep track of where the variable names starts
    char *env_start = NULL;
    // Where we store the retrieved environmental variable value.
    char *ENV = NULL;
    // Get the length of the string.
    if (!str_len) {
        str_len = strlen(str);
    }
    // Position where we are writing on the buffer.
    int b_pos = 0;
    // Iterate the string.
    for (int s_pos = 0; s_pos < str_len; ++s_pos) {
        if ((s_pos == 0) && str[s_pos] == '"')
            continue;
        if ((s_pos == (str_len - 1)) && str[s_pos] == '"')
            continue;
        // If we find the backslash, we need to protect the next character.
        if (str[s_pos] == '\\') {
            if (bit_check(flags, ENV_PROT))
                buf[b_pos++] = '\\';
            else
                bit_set_assign(flags, ENV_PROT);
            continue;
        }
        // If we find the dollar, we need to catch the meaning.
        if (str[s_pos] == '$') {
            // If the previous character is a backslash, we just need to print the dollar.
            if (bit_check(flags, ENV_PROT)) {
                buf[b_pos++] = '$';
            } else if ((s_pos < (str_len - 2)) && ((str[s_pos + 1] == '{'))) {
                // Toggle the open bracket method of accessing env variables.
                bit_set_assign(flags, ENV_BRAK);
                // We need to skip both the dollar and the open bracket `${`.
                env_start = &str[s_pos + 2];
            } else {
                // Toggle the normal method of accessing env variables.
                bit_set_assign(flags, ENV_NORM);
                // We need to skip the dollar `$`.
                env_start = &str[s_pos + 1];
            }
            continue;
        }
        if (bit_check(flags, ENV_BRAK)) {
            if (str[s_pos] == '}') {
                // Copy the environmental variable name.
                strncpy(buffer, env_start, &str[s_pos] - env_start);
                // Search for the environmental variable, and print it.
                if ((ENV = __getenv(buffer)))
                    for (int k = 0; k < strlen(ENV); ++k)
                        buf[b_pos++] = ENV[k];
                // Remove the flag.
                bit_clear_assign(flags, ENV_BRAK);
            }
            continue;
        }
        if (bit_check(flags, ENV_NORM)) {
            if (str[s_pos] == ':') {
                // Copy the environmental variable name.
                strncpy(buffer, env_start, &str[s_pos] - env_start);
                // Search for the environmental variable, and print it.
                if ((ENV = __getenv(buffer)))
                    for (int k = 0; k < strlen(ENV); ++k)
                        buf[b_pos++] = ENV[k];
                // Copy the `:`.
                buf[b_pos++] = str[s_pos];
                // Remove the flag.
                bit_clear_assign(flags, ENV_NORM);
            }
            continue;
        }
        buf[b_pos++] = str[s_pos];
    }
    if (bit_check(flags, ENV_NORM)) {
        // Copy the environmental variable name.
        size_t var_len = str_len - (env_start - str);
        strncpy(buffer, env_start, var_len);
        // Search for the environmental variable, and print it.
        if ((ENV = __getenv(buffer))) {
            for (int k = 0; k < strlen(ENV); ++k) {
                buf[b_pos++] = ENV[k];
            }
        }
    }

    if (null_terminate) {
        buf[b_pos] = 0;
    }
}

static void __expand_env(char *str, char *buf, size_t buf_len)
{
    ___expand_env(str, buf, buf_len, 0, false);
}

static int __export(int argc, char *argv[])
{
    char name[BUFSIZ] = { 0 }, value[BUFSIZ] = { 0 };
    char *first, *last;
    size_t name_len, value_start;
    for (int i = 1; i < argc; ++i) {
        // Get a pointer to the first and last occurrence of `=` inside the argument.
        first = strchr(argv[i], '='), last = strrchr(argv[i], '=');
        // Check validity of first and last, and check that they are the same.
        if (!first || !last || (last < argv[i]) || (first != last))
            continue;
        // Length of the name.
        name_len = last - argv[i];
        // Set where the value starts.
        value_start = (last + 1) - argv[i];
        // Copy the name.
        strncpy(name, argv[i], name_len);
        // Expand the environmental variables inside the argument.
        __expand_env(&argv[i][value_start], value, BUFSIZ);
        // Try to set the environmental variable.
        if ((strlen(name) > 0) && (strlen(value) > 0)) {
            if (setenv(name, value, 1) == -1) {
                printf("Failed to set environmental variable.\n");
                return 1;
            }
        }
    }
    return 0;
}

static int __cd(int argc, char *argv[])
{
    if (argc > 2) {
        printf("%s: too many arguments\n", argv[0]);
        return 1;
    }
    const char *path = NULL;
    if (argc == 2) {
        path = argv[1];
    } else {
        path = getenv("HOME");
        if (path == NULL) {
            printf("cd: There is no home directory set.\n");
            return 1;
        }
    }
    // Get the real path.
    char real_path[PATH_MAX];
    if (realpath(path, real_path, PATH_MAX) != real_path) {
        printf("cd: Failed to resolve directory.\n");
        return 1;
    }
    // Open the given directory.
    int fd = open(real_path, O_RDONLY | O_DIRECTORY, S_IXUSR);
    if (fd == -1) {
        printf("cd: %s: %s\n", real_path, strerror(errno));
        return 1;
    }
    // Set current working directory.
    chdir(real_path);
    close(fd);
    // Get the updated working directory.
    char cwd[PATH_MAX];
    getcwd(cwd, PATH_MAX);
    // Update the environmental variable.
    if (setenv("PWD", cwd, 1) == -1) {
        printf("cd: Failed to set current working directory.\n");
        return 1;
    }
    putchar('\n');
    return 0;
}

/// @brief Gets the options from the command.
/// @param command The executed command.
static void __alloc_argv(char *command, int *argc, char ***argv)
{
    (*argc) = __count_words(command);
    // Get the number of arguments, return if zero.
    if ((*argc) == 0) {
        return;
    }
    (*argv)        = (char **)malloc(sizeof(char *) * ((*argc) + 1));
    bool_t inword  = false;
    char *cit      = command;
    char *argStart = command;
    size_t argcIt  = 0;
    do {
        if (!__is_separator(*cit)) {
            if (!inword) {
                argStart = cit;
                inword   = true;
            }
            continue;
        }

        if (inword) {
            inword = false;
            // Expand possible environment variables in the current argument
            char expand_env_buf[BUFSIZ];
            ___expand_env(argStart, expand_env_buf, BUFSIZ, cit - argStart, true);
            (*argv)[argcIt] = (char *)malloc(strlen(expand_env_buf) + 1);
            strcpy((*argv)[argcIt++], expand_env_buf);
        }
    } while (*cit++);
    (*argv)[argcIt] = NULL;
}

static inline void __free_argv(int argc, char **argv)
{
    for (int i = 0; i < argc; ++i) {
        free(argv[i]);
    }
    free(argv);
}

static void __setup_redirects(int *argcp, char ***argvp)
{
    char **argv = *argvp;
    int argc    = *argcp;

    char *path;
    int flags   = O_CREAT | O_WRONLY;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

    bool_t rd_stdout, rd_stderr;
    rd_stdout = rd_stderr = 0;

    for (int i = 1; i < argc - 1; ++i) {
        if (!strstr(argv[i], ">")) {
            continue;
        }

        path = argv[i + 1];

        // Determine stream to redirect
        switch (*argv[i]) {
        case '&':
            rd_stdout = rd_stderr = true;
            break;
        case '2':
            rd_stderr = true;
            break;
        case '>':
            rd_stdout = true;
            break;
        default:
            continue;
        }

        // Determine open flags
        if (strstr(argv[i], ">>")) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }

        // Remove redirects from argv
        *argcp -= 2;
        free(argv[i]);
        (*argvp)[i] = 0;
        free(argv[i + 1]);
        (*argvp)[i + 1] = 0;

        int fd = open(path, flags, mode);
        if (fd < 0) {
            printf("%s: Failed to open file\n", path);
            exit(1);
        }

        if (rd_stdout) {
            close(STDOUT_FILENO);
            dup(fd);
        }

        if (rd_stderr) {
            close(STDERR_FILENO);
            dup(fd);
        }
        close(fd);
        break;
    }
}

static int __execute_cmd(char* command)
{
    int _status = 0;
    // Retrieve the options from the command.
    // The current number of arguments.
    int _argc = 1;
    // The vector of arguments.
    char **_argv;
    __alloc_argv(command, &_argc, &_argv);
    // Check if the command is empty.
    if (_argc == 0) {
        return 0;
    }

    if (!strcmp(_argv[0], "init")) {
    } else if (!strcmp(_argv[0], "cd")) {
        __cd(_argc, _argv);
    } else if (!strcmp(_argv[0], "..")) {
        const char *__argv[] = { "cd", "..", NULL };
        __cd(2, (char **)__argv);
    } else if (!strcmp(_argv[0], "export")) {
        __export(_argc, _argv);
    } else {
        bool_t blocking = true;
        if (strcmp(_argv[_argc - 1], "&") == 0) {
            blocking = false;
            _argc--;
            free(_argv[_argc]);
            _argv[_argc] = NULL;
        }

        __block_sigchld();

        // Is a shell path, execute it!
        pid_t cpid = fork();
        if (cpid == 0) {
            // Makes the new process a group leader
            pid_t pid = getpid();
            setpgid(cpid, pid);

            __unblock_sigchld();

            __setup_redirects(&_argc, &_argv);

            if (execvp(_argv[0], _argv) == -1) {
                printf("\nUnknown command: %s\n", _argv[0]);
                exit(127);
            }
        }
        if (blocking) {
            waitpid(cpid, &_status, 0);
            if (WIFSIGNALED(_status)) {
                printf(FG_RED "\nExit status %d, killed by signal %d\n" FG_RESET,
                       WEXITSTATUS(_status), WTERMSIG(_status));
            } else if (WIFSTOPPED(_status)) {
                printf(FG_YELLOW "\nExit status %d, stopped by signal %d\n" FG_RESET,
                       WEXITSTATUS(_status), WSTOPSIG(_status));
            } else if (WEXITSTATUS(_status) != 0) {
                printf(FG_RED "\nExit status %d\n" FG_RESET, WEXITSTATUS(_status));
            }
        }
        __unblock_sigchld();
    }
    // Free up the memory reserved for the arguments.
    __free_argv(_argc, _argv);
    status = WEXITSTATUS(_status);
    return status;
}

static int __execute_file(char *path)
{
    int fd;
    if ((fd = open(path, O_RDONLY, 0)) == -1) {
        printf("%s: %s\n", path, strerror(errno));
        return -errno;
    }
    char cmd[BUFSIZ];
    while (fgets(cmd, sizeof(cmd), fd)) {
        if (cmd[0] == '#') {
            continue;
        }

        if ((status = __execute_cmd(cmd)) != 0) {
            printf("\n%s: exited with %d\n", cmd, status);
        }
    }

    close(fd);
    return status;
}

static void __interactive_mode(void)
{
    stat_t buf;
    if (stat(".shellrc", &buf) == 0) {
        int ret = __execute_file(".shellrc");
        if (ret < 0) {
            printf("%s: .shellrc: %s\n", strerror(-ret));
        }
    }
    // Enable the readline history
    using_history();
#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
    while (true) {
        // First print the prompt.
        __prompt_print();
        // Get the input command.
        char *cmd = readline(NULL);
        __execute_cmd(cmd);
        free(cmd);
    }
#pragma clang diagnostic pop
}

void wait_for_child(int signum)
{
    wait(NULL);
}

int main(int argc, char *argv[])
{
    setsid();

    struct termios _termios;
    tcgetattr(STDIN_FILENO, &_termios);
    _termios.c_lflag &= ~ISIG;
    tcsetattr(STDIN_FILENO, 0, &_termios);

    char *USER = getenv("USER");
    if (USER == NULL) {
        printf("shell: There is no user set.\n");
        return 1;
    }
    if (getenv("PATH") == NULL) {
        if (setenv("PATH", "/bin:/usr/bin", 0) == -1) {
            printf("shell: Failed to set PATH.\n");
            return 1;
        }
    }
    // Set the signal handler to handle the termination of the child.
    sigaction_t action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = wait_for_child;
    if (sigaction(SIGCHLD, &action, NULL) == -1) {
        printf("Failed to set signal handler (%s).\n", SIGCHLD, strerror(errno));
        return 1;
    }

    // We have been executed as script interpreter
    if (!strstr(argv[0], "shell")) {
        return __execute_file(argv[1]);
    }

    // Interactive
    if (argc == 1) {
        // Move inside the home directory.
        __cd(0, NULL);
        __interactive_mode();
    } else {
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "-c") == 0) {
                if (i+1 == argc) {
                    errx(2, "%s: -c: option requires an argument", argv[0]);
                }

                if (!(status = __execute_cmd(argv[i+1]))) {
                    return status;
                }
                // Skip the next argument
                i++;
            }
            else if (!(status = __execute_file(argv[i]))) {
                return status;
            }
        }
    }

    return 0;
}
