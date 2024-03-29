/// @file readline.c
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

/// Maximum length of commands.
#define LINE_LEN 64
/// Maximum lenght of the history.
#define HISTORY_MAX 10

// The input command.
static char line[LINE_LEN] = { 0 };
// The index of the cursor.
static size_t cursor_index = 0;
// History of commands.
static char history[HISTORY_MAX][LINE_LEN] = { 0 };
// The current write index inside the history.
static int history_write_index = 0;
// The current read index inside the history.
static int history_read_index = 0;
// Boolean used to check if the history is full.
static bool_t history_full = false;
// Boolean used to check if the history is used.
static bool_t use_history = false;

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

void using_history(void)
{
    use_history = true;
}

/// @brief Push the command inside the history.
static inline void history_push(char *_line)
{
    if (!use_history) return;

    // Reset the read index.
    history_read_index = history_write_index;
    // Check if it is a duplicated entry.
    if (history_write_index > 0) {
        if (strcmp(history[history_write_index - 1], _line) == 0) {
            return;
        }
    }
    // Insert the node.
    strcpy(history[history_write_index], _line);
    if (++history_write_index >= HISTORY_MAX) {
        history_write_index = 0;
        history_full        = true;
    }
    // Reset the read index.
    history_read_index = history_write_index;
}

/// @brief Give the key allows to navigate through the history.
static char *history_fetch(bool_t up)
{
    if (!use_history || ((history_write_index == 0) && (history_full == false))) {
        return NULL;
    }
    // If the history is empty do nothing.
    char *_line = NULL;
    // Update the position inside the history.
    int next_index = history_read_index + (up ? -1 : +1);
    // Check the next index.
    if (history_full) {
        if (next_index < 0) {
            next_index = HISTORY_MAX - 1;
        } else if (next_index >= HISTORY_MAX) {
            next_index = 0;
        }
        // Do no read where ne will have to write next.
        if (next_index == history_write_index) {
            next_index = history_read_index;
            return NULL;
        }
    } else {
        if (next_index < 0) {
            next_index = 0;
        } else if (next_index >= history_write_index) {
            next_index = history_read_index;
            return NULL;
        }
    }
    history_read_index = next_index;
    _line              = history[history_read_index];
    // Return the command.
    return _line;
}

/// @brief Completely delete the current line.
static inline void readline_clr(void)
{
    // First we need to get back to the end of the line.
    while (line[cursor_index] != 0) {
        ++cursor_index;
        puts("\033[1C");
    }
    memset(line, '\0', LINE_LEN);
    // Then we delete all the character.
    for (size_t it = 0; it < cursor_index; ++it) {
        putchar('\b');
    }
    // Reset the index.
    cursor_index = 0;
}

/// @brief Replaces the current line with a new one.
static inline void readline_replace(char *new_line)
{
    // Outputs the command.
    printf(new_line);
    // Moves the cursor.
    cursor_index += strlen(new_line);
    // Copies the command.
    strcpy(line, new_line);
}

/// @brief Erases one character from the console.
static inline void readline_ers(char c)
{
    if ((c == '\b') && (cursor_index > 0)) {
        strcpy(line + cursor_index - 1, line + cursor_index);
        putchar('\b');
        --cursor_index;
    } else if ((c == 0x7F) && (line[0] != 0) && ((cursor_index + 1) < LINE_LEN)) {
        strcpy(line + cursor_index, line + cursor_index + 1);
        putchar(0x7F);
    }
}

/// @brief Appends the character `c` on the command.
static inline int readline_app(char c)
{
    if ((cursor_index + 1) < LINE_LEN) {
        // If at the current index there is a character, shift the entire
        // command ahead.
        if (line[cursor_index] != 0) {
            // Move forward the entire string.
            for (unsigned long i = strlen(line); i > cursor_index; --i) {
                line[i] = line[i - 1];
            }
        }
        // Place the new character.
        line[cursor_index++] = c;
        return 1;
    }
    return 0;
}

static inline void readline_sug(dirent_t *suggestion, size_t starting_position)
{
    if (suggestion) {
        for (size_t i = starting_position; i < strlen(suggestion->d_name); ++i) {
            if (readline_app(suggestion->d_name[i])) {
                putchar(suggestion->d_name[i]);
            }
        }
        // If we suggested a directory, append a slash.
        if (suggestion->d_type == DT_DIR) {
            if (readline_app('/')) {
                putchar('/');
            }
        }
    }
}

static void readline_complete(void)
{
    // Get the lenght of the command.
    size_t line_len = strlen(line);
    // Count the number of words.
    int words = __count_words(line);
    // If there are no words, skip.
    if (words == 0) {
        return;
    }
    // Determines if we are at the beginning of a new argument, last character is space.
    if (__is_separator(line[line_len - 1])) {
        return;
    }
    // If the last two characters are two dots `..` append a slash `/`,
    // and continue.
    if ((line_len >= 2) && ((line[line_len - 2] == '.') && (line[line_len - 1] == '.'))) {
        if (readline_app('/')) {
            putchar('/');
            return;
        }
    }
    char cwd[PATH_MAX];
    getcwd(cwd, PATH_MAX);
    // Determines if we are executing a command from current directory.
    int is_run_line = (words == 1) && (line[0] == '.') && (line_len > 3) && (line[1] == '/');
    // Determines if we are entering an absolute path.
    int is_abs_path = (words == 1) && (line[0] == '/');
    // Prepare the dirent variable.
    dirent_t dent;
    // If there is only one word, we are searching for a command.
    if (is_run_line) {
        if (__folder_contains(cwd, line + 2, 0, &dent)) {
            readline_sug(&dent, line_len - 2);
        }
    } else if (is_abs_path) {
        char _dirname[PATH_MAX];
        if (!dirname(line, _dirname, sizeof(_dirname))) {
            return;
        }
        const char *_basename = basename(line);
        if (!_basename) {
            return;
        }
        if ((*_dirname == 0) || (*_basename == 0)) {
            return;
        }
        if (__folder_contains(_dirname, _basename, 0, &dent)) {
            readline_sug(&dent, strlen(_basename));
        }
    } else if (words == 1) {
        if (__search_in_path(line, &dent)) {
            readline_sug(&dent, line_len);
        }
    } else {
        // Search the last occurrence of a space, from there on
        // we will have the last argument.
        char *last_argument = strrchr(line, ' ');
        // We need to move ahead of one character if we found the space.
        last_argument = last_argument ? last_argument + 1 : NULL;
        // If there is no last argument.
        if (last_argument == NULL) {
            return;
        }
        char _dirname[PATH_MAX];
        if (!dirname(last_argument, _dirname, sizeof(_dirname))) {
            return;
        }
        const char *_basename = basename(last_argument);
        if (!_basename) {
            return;
        }
        if ((*_dirname != 0) && (*_basename != 0)) {
            if (__folder_contains(_dirname, _basename, 0, &dent)) {
                readline_sug(&dent, strlen(_basename));
            }
        } else if (*_basename != 0) {
            if (__folder_contains(cwd, _basename, 0, &dent)) {
                readline_sug(&dent, strlen(_basename));
            }
        }
    }
}

void (*readline_complete_func)(void) = readline_complete;

static void __move_cursor_back(int n)
{
    printf("\033[%dD", n);
    cursor_index -= n;
}

static void __move_cursor_forward(int n)
{
    printf("\033[%dC", n);
    cursor_index += n;
}

/// @brief Gets the inserted command.
char *readline(const char *prompt)
{
    if (prompt) {
        printf("%s", prompt);
    }
    // Re-Initialize the cursor index.
    cursor_index = 0;
    // Initializing the current command line buffer
    memset(line, '\0', LINE_LEN);
    __set_echo(false);
    do {
        int c = getchar();
        // Return Key
        if (c == '\n') {
            putchar('\n');
            // Break the while loop.
            break;
        }
        // It is a special character.
        if (c == '\033') {
            c = getchar();
            if (c == '[') {
                c = getchar(); // Get the char.
                if ((c == 'A') || (c == 'B')) {
                    char *old_line = history_fetch(c == 'A');
                    if (old_line != NULL) {
                        // Clear the current command.
                        readline_clr();
                        // Sets the command.
                        readline_replace(old_line);
                    }
                } else if (c == 'D') {
                    if (cursor_index > 0) {
                        __move_cursor_back(1);
                    }
                } else if (c == 'C') {
                    if ((cursor_index + 1) < LINE_LEN && (cursor_index + 1) <= strlen(line)) {
                        __move_cursor_forward(1);
                    }
                } else if (c == 'H') {
                    __move_cursor_back(cursor_index);
                } else if (c == 'F') {
                    // Compute the offest to the end of the line, and move only if necessary.
                    size_t offset = strlen(line) - cursor_index;
                    if (offset > 0) {
                        __move_cursor_forward(offset);
                    }
                } else if (c == '3') {
                    c = getchar(); // Get the char.
                    if (c == '~') {
                        readline_ers(0x7F);
                    }
                }
            }
        } else if (c == '\b') {
            readline_ers('\b');
        } else if (c == '\t') {
            if (readline_complete_func != NULL) {
                readline_complete_func();
            }
        } else if (c == 127) {
            if ((cursor_index + 1) <= strlen(line)) {
                strcpy(line + cursor_index, line + cursor_index + 1);
                putchar(127);
            }
        } else if (iscntrl(c)) {
            if (c == CTRL('C')) {
                // Re-set the index to the beginning.
                cursor_index = 0;
                // Go to the new line.
                printf("\n");
                // Sets the command.
                readline_replace("\0");
                // Break the while loop.
                break;
            } else if (c == CTRL('U')) {
                // Clear the current command.
                readline_clr();
            } else if (c == CTRL('A')) {
                __move_cursor_back(cursor_index);
            } else if (c == CTRL('E')) {
                // Compute the offest to the end of the line, and move only if necessary.
                size_t offset = strlen(line) - cursor_index;
                if (offset > 0) {
                    __move_cursor_forward(offset);
                }
            } else if (c == CTRL('D')) {
                // Go to the new line.
                printf("\n");
                exit(0);
            }
        } else if ((c > 0) && (c != '\n')) {
            if (readline_app(c)) {
                putchar(c);
            }
        } else {
            pr_debug("Unrecognized character %02x (%c)\n", c, c);
        }
    } while (cursor_index < LINE_LEN);

    // Cleans all blanks at the beginning of the command.
    trim(line);
    __set_echo(true);

    if (use_history) {
        history_push(line);
    }

    return strdup(line);
}
