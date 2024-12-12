/// @file shell.c
/// @brief Implement shell functions.
/// @copyright (c) 2014-2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <err.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <libgen.h>
#include <sys/stat.h>
#include <signal.h>
#include <io/ansi_colors.h>
#include <sys/bitops.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strerror.h>
#include <termios.h>
#include <limits.h>
#include <sys/utsname.h>
#include <readline.h>

#define CMD_LEN 64

// Required by `export`
#define ENV_NORM 1
#define ENV_BRAK 2
#define ENV_PROT 3

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

/// @brief Count the number of words in a given sentence.
/// @param sentence Pointer to the input sentence.
/// @return The number of words in the sentence or -1 if input is invalid.
static inline int __count_words(const char *sentence)
{
    // Check if the input sentence is valid.
    if (sentence == NULL) {
        fprintf(stderr, "__count_words: Invalid input, sentence is NULL.\n");
        return -1; // Return -1 to indicate an error.
    }
    int result     = 0;
    bool_t inword  = false;
    const char *it = sentence;
    // Iterate over each character in the sentence.
    do {
        // Check if the current character is a word separator.
        if (__is_separator(*it)) {
            // If we are currently inside a word, mark the end of the word.
            if (inword) {
                inword = false;
                result++; // Increment the word count.
            }
        } else {
            // If the character is not a separator, mark that we are inside a word.
            inword = true;
        }
    } while (*it++);
    return result;
}

/// @brief Prints the command prompt with user, hostname, time, and current
/// working directory.
static inline void __prompt_print(void)
{
    // Get the current working directory.
    char CWD[PATH_MAX];
    if (getcwd(CWD, PATH_MAX) == NULL) {
        fprintf(stderr, "__prompt_print: Failed to get current working directory.\n");
        strcpy(CWD, "error");
    }
    // Get the HOME environment variable.
    char *HOME = getenv("HOME");
    if (HOME != NULL) {
        // If the current working directory is equal to HOME, replace it with '~'.
        if (strcmp(CWD, HOME) == 0) {
            strcpy(CWD, "~\0");
        }
    }
    // Get the USER environment variable.
    char *USER = getenv("USER");
    if (USER == NULL) {
        fprintf(stderr, "__prompt_print: Failed to get USER environment variable.\n");
        USER = "error";
    }
    // Get the current time.
    time_t rawtime = time(NULL);
    if (rawtime == (time_t)(-1)) {
        fprintf(stderr, "__prompt_print: Failed to get current time.\n");
        rawtime = 0; // Set to 0 in case of failure.
    }
    // Convert time to local time format.
    tm_t *timeinfo = localtime(&rawtime);
    if (timeinfo == NULL) {
        fprintf(stderr, "__prompt_print: Failed to convert time to local time.\n");
        // Use a default value to avoid segmentation faults.
        static tm_t default_time = { 0 };
        timeinfo                 = &default_time;
    }
    // Get the hostname using uname.
    char *HOSTNAME;
    utsname_t buffer;
    if (uname(&buffer) < 0) {
        fprintf(stderr, "__prompt_print: Failed to get hostname using uname.\n");
        HOSTNAME = "error";
    } else {
        HOSTNAME = buffer.nodename;
    }
    // Print the formatted prompt.
    printf(FG_GREEN "%s" FG_WHITE "@" FG_CYAN "%s " FG_BLUE_BRIGHT "[%02d:%02d:%02d]" FG_WHITE " [%s] " FG_RESET "\n-> %% ",
           USER, HOSTNAME, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, CWD);
}

/// @brief Retrieves the value of the specified environment variable or special shell variables.
/// @param var The name of the environment variable or special variable to retrieve.
/// @return Returns the value of the variable if found, or NULL if not.
static char *__getenv(const char *var)
{
    // Ensure the input variable name is valid (non-NULL and non-empty).
    if (var == NULL || strlen(var) == 0) {
        return NULL;
    }
    // If the variable has a length greater than 1, retrieve it as a standard environment variable.
    if (strlen(var) > 1) {
        return getenv(var);
    }
    // Handle special variables like `$?`, which represents the status of the last command.
    if (var[0] == '?') {
        // Assuming 'status' is a global or accessible variable containing the last command status.
        sprintf(status_buf, "%d", status); // Convert the status to a string.
        return status_buf;                 // Return the status as a string.
    }
    // TODO: Implement access to `argv` for positional parameters (e.g., $1, $2).
#if 0
    int arg = strtol(var, NULL, 10);  // Convert the variable name to an integer.
    if (arg < argc) {  // Ensure the argument index is within bounds.
        return argv[arg];  // Return the corresponding argument from `argv`.
    }
#endif
    // If no match is found, return NULL.
    return NULL;
}

/// @brief Expands environmental variables in a string and stores the result in the buffer.
/// @param str The input string containing potential environmental variables.
/// @param str_len The length of the input string.
/// @param buf The buffer where the expanded string will be stored.
/// @param buf_len The maximum length of the buffer.
/// @param null_terminate If true, the resulting buffer will be null-terminated.
static void ___expand_env(char *str, size_t str_len, char *buf, size_t buf_len, bool_t null_terminate)
{
    // Input validation: Ensure that str and buf are not NULL, and that buf_len is valid.
    if ((str == NULL) || (buf == NULL) || (buf_len == 0)) {
        fprintf(stderr, "Error: Invalid input parameters to ___expand_env.\n");
        return;
    }
    // Buffer where we store the name of the variable.
    char buffer[BUFSIZ] = { 0 };
    // Flags used to keep track of the special characters.
    unsigned flags = 0;
    // Pointer to where the variable name starts.
    char *env_start = NULL;
    // Pointer to store the retrieved environmental variable value.
    char *ENV = NULL;
    // Position where we are writing on the buffer.
    int b_pos = 0;
    // Iterate through the input string.
    for (int s_pos = 0; s_pos < str_len; ++s_pos) {
        // Skip quotes at the start and end of the string.
        if ((s_pos == 0 && str[s_pos] == '"') || (s_pos == (str_len - 1) && str[s_pos] == '"')) {
            continue;
        }
        // Handle backslash as escape character.
        if (str[s_pos] == '\\') {
            if (bit_check(flags, ENV_PROT)) {
                if (b_pos < buf_len - 1) {
                    buf[b_pos++] = '\\'; // If protected, add the backslash.
                }
            } else {
                bit_set_assign(flags, ENV_PROT); // Set the protection flag.
            }
            continue;
        }
        // Handle environmental variable expansion with $.
        if (str[s_pos] == '$') {
            if (bit_check(flags, ENV_PROT)) {
                // If protected by backslash, just add the dollar sign.
                if (b_pos < buf_len - 1) {
                    buf[b_pos++] = '$';
                }
            } else if ((s_pos < (str_len - 2)) && (str[s_pos + 1] == '{')) {
                // Handle ${VAR} syntax for environmental variables.
                bit_set_assign(flags, ENV_BRAK);
                env_start = &str[s_pos + 2]; // Start of the variable name.
            } else {
                // Handle normal $VAR syntax.
                bit_set_assign(flags, ENV_NORM);
                env_start = &str[s_pos + 1]; // Start of the variable name.
            }
            continue;
        }
        // Handle ${VAR} style environmental variables.
        if (bit_check(flags, ENV_BRAK)) {
            if (str[s_pos] == '}') {
                // Extract and expand the environmental variable name.
                strncpy(buffer, env_start, &str[s_pos] - env_start);
                buffer[&str[s_pos] - env_start] = '\0'; // Null-terminate.
                // Retrieve the value of the environmental variable.
                if ((ENV = __getenv(buffer)) != NULL) {
                    // Copy the value into the buffer.
                    for (int k = 0; k < strlen(ENV) && b_pos < buf_len - 1; ++k) {
                        buf[b_pos++] = ENV[k];
                    }
                }
                bit_clear_assign(flags, ENV_BRAK); // Clear the flag.
            }
            continue;
        }
        // Handle $VAR style environmental variables.
        if (bit_check(flags, ENV_NORM)) {
            if (str[s_pos] == ':') {
                strncpy(buffer, env_start, &str[s_pos] - env_start);
                buffer[&str[s_pos] - env_start] = '\0'; // Null-terminate.
                // Retrieve the value of the environmental variable.
                if ((ENV = __getenv(buffer)) != NULL) {
                    for (int k = 0; k < strlen(ENV) && b_pos < buf_len - 1; ++k) {
                        buf[b_pos++] = ENV[k];
                    }
                }
                // Add the ':' character and clear the flag.
                if (b_pos < buf_len - 1) {
                    buf[b_pos++] = str[s_pos];
                }
                bit_clear_assign(flags, ENV_NORM);
            }
            continue;
        }
        // Add normal characters to the buffer.
        if (b_pos < buf_len - 1) {
            buf[b_pos++] = str[s_pos];
        }
    }
    // Handle any remaining environmental variable in $VAR style.
    if (bit_check(flags, ENV_NORM)) {
        strncpy(buffer, env_start, str_len - (env_start - str));
        buffer[str_len - (env_start - str)] = '\0';
        if ((ENV = __getenv(buffer)) != NULL) {
            for (int k = 0; k < strlen(ENV) && b_pos < buf_len - 1; ++k) {
                buf[b_pos++] = ENV[k];
            }
        }
    }
    // Null-terminate the buffer if requested.
    if (null_terminate && b_pos < buf_len) {
        buf[b_pos] = '\0';
    }
}

/// @brief Simplified version of ___expand_env for use without specifying length and null-termination.
/// @param str The input string containing environmental variables.
/// @param buf The buffer where the expanded result will be stored.
/// @param buf_len The size of the buffer.
static void __expand_env(char *str, char *buf, size_t buf_len)
{
    ___expand_env(str, strlen(str), buf, buf_len, false);
}

/// @brief Sets environment variables based on arguments.
/// @param argc The number of arguments passed.
/// @param argv The array of arguments, where each argument is a name-value pair (e.g., NAME=value).
/// @return Returns 0 on success, 1 on failure.
static int __export(int argc, char *argv[])
{
    char name[BUFSIZ] = { 0 }, value[BUFSIZ] = { 0 };
    char *first, *last;
    size_t name_len, value_start;
    // Loop through each argument, starting from argv[1].
    for (int i = 1; i < argc; ++i) {
        // Get a pointer to the first and last occurrence of `=` inside the argument.
        first = strchr(argv[i], '=');
        last  = strrchr(argv[i], '=');
        // Check the validity of first and last, and ensure they are the same (i.e., a single `=`).
        if (!first || !last || (last < argv[i]) || (first != last)) {
            printf("Invalid format: '%s'. Expected NAME=value format.\n", argv[i]);
            continue; // Skip this argument if invalid.
        }
        // Calculate the length of the name (part before `=`).
        name_len = last - argv[i];

        // Ensure that the name is not empty.
        if (name_len == 0) {
            printf("Invalid format: '%s'. Name cannot be empty.\n", argv[i]);
            continue; // Skip this argument if the name is empty.
        }
        // Set the starting index of the value (part after `=`).
        value_start = (last + 1) - argv[i];
        // Copy the name into the `name` buffer.
        strncpy(name, argv[i], name_len);
        name[name_len] = '\0'; // Null-terminate the name string.
        // Expand the environmental variables inside the value part of the argument.
        __expand_env(&argv[i][value_start], value, BUFSIZ);
        // Try to set the environmental variable if both name and value are valid.
        if ((strlen(name) > 0) && (strlen(value) > 0)) {
            if (setenv(name, value, 1) == -1) {
                printf("Failed to set environmental variable: %s\n", name);
                return 1; // Return 1 on failure to set the environment variable.
            }
        } else {
            printf("Invalid variable assignment: '%s'. Name and value must be non-empty.\n", argv[i]);
        }
    }
    return 0; // Return 0 on success.
}

/// @brief Changes the current working directory.
/// @param argc The number of arguments passed.
/// @param argv The array of arguments, where argv[0] is the command and argv[1] is the target directory.
/// @return Returns 0 on success, 1 on failure.
static int __cd(int argc, char *argv[])
{
    // Check if too many arguments are provided.
    if (argc > 2) {
        puts("cd: too many arguments\n");
        return 1;
    }
    // Determine the path to change to.
    const char *path = NULL;
    if (argc == 2) {
        path = argv[1];
    } else {
        // If no argument is provided, use the HOME directory.
        path = getenv("HOME");
        if (path == NULL) {
            puts("cd: There is no home directory set.\n");
            return 1;
        }
    }
    // Get the real path of the target directory.
    char real_path[PATH_MAX];
    if (realpath(path, real_path, PATH_MAX) != real_path) {
        printf("cd: Failed to resolve directory '%s': %s\n", path, strerror(errno));
        return 1;
    }
    // Stat the directory to ensure it exists and get information about it.
    stat_t dstat;
    if (stat(real_path, &dstat) == -1) {
        printf("cd: cannot stat '%s': %s\n", real_path, strerror(errno));
        return 1;
    }
    // Check if the path is a symbolic link.
    if (S_ISLNK(dstat.st_mode)) {
        char link_buffer[PATH_MAX];
        ssize_t len;
        // Read the symbolic link.
        if ((len = readlink(real_path, link_buffer, sizeof(link_buffer) - 1)) < 0) {
            printf("cd: Failed to read symlink '%s': %s\n", real_path, strerror(errno));
            return 1;
        }
        // Null-terminate the link buffer.
        link_buffer[len] = '\0';
        // Resolve the symlink to an absolute path.
        if (realpath(link_buffer, real_path, PATH_MAX) != real_path) {
            printf("cd: Failed to resolve symlink '%s': %s\n", link_buffer, strerror(errno));
            return 1;
        }
    }
    // Open the directory to ensure it's accessible.
    int fd = open(real_path, O_RDONLY | O_DIRECTORY, S_IXUSR | S_IXOTH);
    if (fd == -1) {
        printf("cd: %s: %s\n", real_path, strerror(errno));
        return 1;
    }
    // Change the current working directory.
    if (chdir(real_path) == -1) {
        printf("cd: Failed to change directory to '%s': %s\n", real_path, strerror(errno));
        close(fd);
        return 1;
    }
    // Close the directory file descriptor.
    close(fd);
    // Get the updated current working directory.
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        printf("cd: Failed to get current working directory: %s\n", strerror(errno));
        return 1;
    }
    // Update the PWD environment variable to the new directory.
    if (setenv("PWD", cwd, 1) == -1) {
        printf("cd: Failed to set current working directory in environment: %s\n", strerror(errno));
        return 1;
    }
    putchar('\n');
    return 0;
}

/// @brief Allocates and parses the arguments (argv) from the provided command string.
/// @param command The executed command.
/// @param argc Pointer to the argument count (to be set by this function).
/// @param argv Pointer to the argument list (to be set by this function).
static void __alloc_argv(char *command, int *argc, char ***argv)
{
    // Input validation: Check if command, argc, or argv are NULL.
    if (command == NULL || argc == NULL || argv == NULL) {
        printf("Error: Invalid input. 'command', 'argc', or 'argv' is NULL.\n");
        return;
    }
    // Count the number of words (arguments) in the command.
    (*argc) = __count_words(command);
    // If there are no arguments, return early.
    if ((*argc) == 0) {
        *argv = NULL;
        return;
    }
    // Allocate memory for the arguments array (argc + 1 for the NULL terminator).
    (*argv) = (char **)malloc(sizeof(char *) * ((*argc) + 1));
    if (*argv == NULL) {
        printf("Error: Failed to allocate memory for arguments.\n");
        return;
    }
    bool_t inword  = false;
    char *cit      = command; // Command iterator.
    char *argStart = command; // Start of the current argument.
    size_t argcIt  = 0;       // Iterator for arguments.
    // Iterate through the command string.
    do {
        // Check if the current character is not a separator (indicating part of a word).
        if (!__is_separator(*cit)) {
            if (!inword) {
                // If we are entering a new word, mark the start of the argument.
                argStart = cit;
                inword   = true;
            }
            continue;
        }
        // If we were inside a word and encountered a separator, process the word.
        if (inword) {
            inword = false;
            // Expand possible environment variables in the current argument.
            char expand_env_buf[BUFSIZ];
            ___expand_env(argStart, cit - argStart, expand_env_buf, BUFSIZ, true);
            // Allocate memory for the expanded argument.
            (*argv)[argcIt] = (char *)malloc(strlen(expand_env_buf) + 1);
            if ((*argv)[argcIt] == NULL) {
                printf("Error: Failed to allocate memory for argument %zu.\n", argcIt);
                // Free previously allocated arguments to prevent memory leaks.
                for (size_t j = 0; j < argcIt; ++j) {
                    free((*argv)[j]);
                }
                free(*argv);
                *argv = NULL;
                return;
            }
            // Copy the expanded argument to the argv array.
            strcpy((*argv)[argcIt++], expand_env_buf);
        }
    } while (*cit++);
    // Null-terminate the argv array.
    (*argv)[argcIt] = NULL;
}

/// @brief Frees the memory allocated for argv and its arguments.
/// @param argc The number of arguments.
/// @param argv The array of argument strings to be freed.
static inline void __free_argv(int argc, char **argv)
{
    // Input validation: Check if argv is NULL before proceeding.
    if (argv == NULL) {
        return;
    }
    // Free each argument in the argv array.
    for (int i = 0; i < argc; ++i) {
        if (argv[i] != NULL) {
            free(argv[i]);
        }
    }
    // Free the argv array itself.
    free(argv);
}

/// @brief Sets up file redirections based on arguments.
/// @param argcp Pointer to the argument count (to be updated if redirects are removed).
/// @param argvp Pointer to the argument list (to be updated if redirects are removed).
static void __setup_redirects(int *argcp, char ***argvp)
{
    char **argv = *argvp;
    int argc    = *argcp;

    char *path;
    int flags   = O_CREAT | O_WRONLY;
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

    bool_t rd_stdout = false;
    bool_t rd_stderr = false;

    // Iterate through arguments to find redirects.
    for (int i = 1; i < argc - 1; ++i) {
        // Skip if the argument doesn't contain '>'.
        if (!strstr(argv[i], ">")) {
            continue;
        }
        // Check if the next argument (i + 1) is within bounds.
        if (i + 1 >= argc || argv[i + 1] == NULL) {
            printf("Error: Missing path for redirection after '%s'.\n", argv[i]);
            exit(1); // Exit if no path is provided for redirection.
        }
        path = argv[i + 1]; // Set the path for redirection.
        // Determine the stream to redirect based on the first character of the argument.
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
            continue; // If no valid redirection is found, continue.
        }
        // Determine the open flags for append or truncate.
        if (strstr(argv[i], ">>")) {
            flags |= O_APPEND;
        } else {
            flags |= O_TRUNC;
        }
        // Remove redirection arguments from argv.
        *argcp -= 2;
        free(argv[i]);
        (*argvp)[i] = NULL;
        free(argv[i + 1]);
        (*argvp)[i + 1] = NULL;
        // Open the file for redirection.
        int fd = open(path, flags, mode);
        if (fd < 0) {
            printf("Error: Failed to open file '%s' for redirection.\n", path);
            exit(1);
        }
        // Redirect stdout if necessary.
        if (rd_stdout) {
            close(STDOUT_FILENO);
            if (dup(fd) < 0) {
                printf("Error: Failed to redirect stdout to file '%s'.\n", path);
                close(fd);
                exit(1);
            }
        }
        if (rd_stderr) {
            close(STDERR_FILENO);
            if (dup(fd) < 0) {
                printf("Error: Failed to redirect stderr to file '%s'.\n", path);
                close(fd);
                exit(1);
            }
        }
        close(fd); // Close the file descriptor after redirection.
        break;     // Stop after handling one redirection.
    }
}

/// @brief Executes a given command.
/// @param command The command to execute.
/// @return Returns the exit status of the command.
static int __execute_command(char *command)
{
    int _status = 0;

    // Retrieve the arguments from the command buffer.
    int _argc = 1; // Initialize the argument count.
    char **_argv;  // Argument vector.

    __alloc_argv(command, &_argc, &_argv);

    // Check if the command is empty (no arguments parsed).
    if (_argc == 0) {
        return 0;
    }

    // Handle built-in commands.
    if (!strcmp(_argv[0], "init")) {
        // Placeholder for the 'init' command.
    } else if (!strcmp(_argv[0], "cd")) {
        // Execute the 'cd' command.
        __cd(_argc, _argv);
    } else if (!strcmp(_argv[0], "..")) {
        // Shortcut for 'cd ..'.
        const char *__argv[] = { "cd", "..", NULL };
        __cd(2, (char **)__argv);
    } else if (!strcmp(_argv[0], "export")) {
        // Execute the 'export' command.
        __export(_argc, _argv);
    } else {
        // Handle external commands (executed as child processes).
        bool_t blocking = true;
        // Check if the command should be run in the background (indicated by '&').
        if (strcmp(_argv[_argc - 1], "&") == 0) {
            blocking = false;   // Non-blocking execution (background process).
            _argc--;            // Remove the '&' from the argument list.
            free(_argv[_argc]); // Free the memory for the '&'.
            _argv[_argc] = NULL;
        }
        // Block SIGCHLD signal to prevent interference with child processes.
        __block_sigchld();
        // Fork the current process to create a child process.
        pid_t cpid = fork();
        if (cpid == 0) {
            // Child process: Execute the command.
            pid_t pid = getpid();
            setpgid(cpid, pid);  // Make the new process a group leader.
            __unblock_sigchld(); // Unblock SIGCHLD signals in the child process.
            // Handle redirections (e.g., stdout, stderr).
            __setup_redirects(&_argc, &_argv);
            // Attempt to execute the command using execvp.
            if (execvp(_argv[0], _argv) == -1) {
                printf("\nUnknown command: %s\n", _argv[0]);
                exit(127); // Exit with status 127 if the command is not found.
            }
        }
        if (blocking) {
            // Parent process: Wait for the child process to finish.
            waitpid(cpid, &_status, 0);
            // Handle different exit statuses of the child process.
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
        __unblock_sigchld(); // Unblock SIGCHLD signals after command execution.
    }
    // Free the memory allocated for the argument list.
    __free_argv(_argc, _argv);
    // Update the global status variable with the exit status of the command.
    status = WEXITSTATUS(_status);
    return status; // Return the exit status of the command.
}

static int __execute_file(char *path)
{
    char command_buffer[CMD_LEN];
    int fd;
    if ((fd = open(path, O_RDONLY, 0)) == -1) {
        printf("%s: %s\n", path, strerror(errno));
        return -errno;
    }
    while (fgets(command_buffer, sizeof(command_buffer), fd)) {
        if (command_buffer[0] == '#') {
            continue;
        }

        if ((status = __execute_command(command_buffer)) != 0) {
            printf("\n%s: exited with %d\n", command_buffer, status);
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
#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
    while (true) {
        // First print the prompt.
        __prompt_print();

        // Get the input command.
        char* command = readline(NULL);
        if (!command) {
            fprintf(stderr, "Error reading command...\n");
            continue;
        }

        // Execute the command.
        __execute_command(command);
        free(command);
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
    // Initialize the history.
    using_history();

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
        // check file arguments
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "-c") == 0) {
                if (i+1 == argc) {
                    errx(2, "%s: -c: option requires an argument", argv[0]);
                }

                if (!(status = __execute_command(argv[i+1]))) {
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
