/// @file readline.c
/// @brief get a line from a user with editing
/// @copyright (c) 2014-2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include "readline.h"

#include <err.h>
#include <fcntl.h>
#include <io/ansi_colors.h>
#include <libgen.h>
#include <limits.h>
#include <ring_buffer.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <strerror.h>
#include <string.h>
#include <sys/bitops.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>

/// Maximum length of a line.
#define LINE_LEN 64
/// Maximum lenght of the history.
#define HISTORY_MAX 10

// The index of the cursor.
static size_t cursor_index = 0;
// The length of the input read so far.
static size_t length = 0;

static bool_t use_history = false;

static inline void rb_history_entry_copy(char *dest, const char *src, unsigned size)
{
    strncpy(dest, src, size);
}

/// Initialize the two-dimensional ring buffer for integers.
DECLARE_FIXED_SIZE_2D_RING_BUFFER(char, history, HISTORY_MAX, LINE_LEN, 0)

// History of commands.
static rb_history_t history;
// History reading cursor_index.
static unsigned history_cursor_index;

// The input buffer.
static rb_history_entry_t line;

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

/// @brief Checks if a specified folder contains a specific entry of a certain type.
/// @param folder The path to the folder to search in.
/// @param entry The name of the entry to search for.
/// @param accepted_type The type of entry to search for (e.g., file, directory).
/// @param result Pointer to store the found entry, if any.
/// @return Returns 1 if the entry is found, 0 otherwise or in case of an error.
static inline int __folder_contains(
    const char *folder,
    const char *entry,
    int accepted_type,
    dirent_t *result)
{
    // Validate input parameters.
    if ((folder == NULL) || (entry == NULL) || (result == NULL)) {
        return 0; // Return 0 to indicate an error.
    }
    // Attempt to open the folder with read-only and directory flags.
    int fd = open(folder, O_RDONLY | O_DIRECTORY, 0);
    if (fd == -1) {
        return 0; // Return 0 if the folder couldn't be opened.
    }
    // Prepare variables for the search.
    dirent_t dent;    // Variable to hold the directory entry during iteration.
    size_t entry_len; // Length of the entry name.
    int found = 0;    // Flag to indicate if the entry was found.
    // Calculate the length of the entry name.
    entry_len = strlen(entry);
    if (entry_len == 0) {
        close(fd); // Close the folder before returning.
        return 0;  // Return 0 if the entry name is empty.
    }
    // Iterate over the directory entries.
    while (getdents(fd, &dent, sizeof(dirent_t)) == sizeof(dirent_t)) {
        // If an accepted type is specified and doesn't match the current entry type, skip.
        if (accepted_type && (accepted_type != dent.d_type)) {
            continue;
        }
        // Compare the entry name with the current directory entry name.
        if (strncmp(entry, dent.d_name, entry_len) == 0) {
            // If a match is found, store the result and mark the entry as found.
            *result = dent;
            found   = 1;
            break; // Exit the loop as the entry was found.
        }
    }
    // Close the directory file descriptor.
    close(fd);
    // Return whether the entry was found.
    return found;
}

/// @brief Searches for a specified entry in the system's PATH directories.
/// @param entry The name of the entry to search for.
/// @param result Pointer to store the found entry, if any.
/// @return Returns 1 if the entry is found, 0 otherwise.
static inline int __search_in_path(const char *entry, dirent_t *result)
{
    // Validate input parameters.
    if ((entry == NULL) || (result == NULL)) {
        fprintf(stderr, "__search_in_path: Invalid input parameters.\n");
        return 0; // Return 0 to indicate an error.
    }
    // Retrieve the PATH environment variable.
    char *PATH_VAR = getenv("PATH");
    if (PATH_VAR == NULL) {
        // If PATH is not set, default to commonly used binary directories.
        PATH_VAR = "/bin:/usr/bin";
    }
    // Prepare for tokenizing the path using custom logic.
    char token[NAME_MAX] = { 0 }; // Buffer to hold each token (directory).
    size_t offset        = 0;     // Offset for the tokenizer.
    // Iterate through each directory in the PATH.
    while (tokenize(PATH_VAR, ":", &offset, token, NAME_MAX)) {
        // Search for the entry in the current directory (tokenized directory).
        if (__folder_contains(token, entry, DT_REG, result)) {
            return 1; // Return 1 if the entry is found.
        }
    }
    return 0; // Return 0 if the entry was not found.
}

void using_history(void) {
    use_history = true;
    rb_history_init(&history, rb_history_entry_copy);
}

/// @brief Push the line into the history.
/// @return Returns 1 if the entry was successfully added, 0 if it was a duplicate.
static inline int history_push(void)
{
    if (!use_history) return -1;
    rb_history_entry_t previous_entry;
    // Check if there's an existing entry at the back of the history.
    if (!rb_history_peek_back(&history, &previous_entry)) {
        // Compare the new entry with the last entry to avoid duplicates.
        if (strcmp(line.buffer, previous_entry.buffer) == 0) {
            // Return 0 if the new entry is the same as the previous one (duplicate).
            return 0;
        }
    }
    // Push the new entry to the back of the history ring buffer.
    rb_history_push_back(&history, &line);
    // Set the history cursor_index to the current count, pointing to the end.
    history_cursor_index = history.count;
    // Return 1 to indicate the new entry was successfully added.
    return 1;
}

/// @brief Give the key allows to navigate through the history.
static rb_history_entry_t *history_fetch(char direction)
{
    // If history is empty, return NULL.
    if (history.count == 0) {
        return NULL;
    }
    // Move to the previous entry if direction is UP and cursor_index is greater than 0.
    if ((direction == 'A') && (history_cursor_index > 0)) {
        history_cursor_index--;
    }
    // Move to the next entry if direction is DOWN and cursor_index is less than the history count.
    else if ((direction == 'B') && (history_cursor_index < history.count)) {
        history_cursor_index++;
    }
    // Check if we reached the end of the history in DOWN direction.
    if ((direction == 'B') && (history_cursor_index == history.count)) {
        return NULL;
    }
    // Return the current history entry, adjusting for buffer wrap-around.
    return history.buffer + ((history.tail + history_cursor_index) % history.size);
}

/// @brief Append a character to the history entry buffer.
/// @param entry Pointer to the history entry structure.
/// @param cursor_index Pointer to the current cursor_index in the buffer.
/// @param length Pointer to the current length of the buffer.
/// @param c Character to append to the buffer.
/// @return Returns 1 if the buffer limit is reached, 0 otherwise.
static inline int readline_append(char c)
{
    // Ensure cursor_index does not exceed the buffer size limit.
    if (cursor_index >= line.size) {
        fprintf(stderr, "readline: Index exceeds buffer size.\n");
        return 1;
    }
    // Insert the new character at the current cursor_index in the buffer, then
    // increment the cursor_index.
    line.buffer[cursor_index++] = c;
    // Increase the length of the buffer to reflect the added character.
    length++;
    // Display the newly added character to the standard output.
    putchar(c);
    // Check if the buffer limit has been reached.
    if (cursor_index == (line.size - 1)) {
        // Null-terminate the buffer to ensure it is a valid string.
        line.buffer[cursor_index] = 0;
        // Indicate that the buffer is full.
        return 1;
    }
    // The buffer limit has not been reached.
    return 0;
}

/// @brief Clears the current command from both the display and the buffer.
static inline void readline_clear(void) {
    // Ensure cursor_index and length are greater than zero and cursor_index does not exceed
    // length.
    if (cursor_index < 0 || length < 0 || cursor_index > length) {
        fprintf(stderr, "readline: Invalid cursor_index or length values: cursor_index=%d, length=%d.\n", cursor_index, length);
        return;
    }
    // Move the cursor to the end of the current command.
    printf("\033[%dC", length - cursor_index);
    // Clear the current command from the display by moving backwards.
    while (length--) { putchar('\b'); }
    // Clear the current command from the buffer by setting it to zero.
    memset(line.buffer, 0, line.size);
    // Reset both cursor_index and length to zero, as the command is now cleared.
    cursor_index = length = 0;
}

/// @brief Suggests a directory entry to be appended to the current command
/// buffer.
/// @param filename Pointer to the file name.
/// @param filetype The file type.
static inline void readline_suggest(const char *filename, int filetype, int offset)
{
    // Check if there is a valid suggestion to process.
    if (filename) {
        // Iterate through the characters of the suggested directory entry name.
        for (int i = offset; i < strlen(filename); ++i) {
            // If there is a character inside the buffer, we
            if (line.buffer[cursor_index]) {
                if (line.buffer[cursor_index] != filename[i]) {
                    return;
                } else if (line.buffer[cursor_index] != ' ') {
                    // Move the cursor right by 1.
                    puts("\033[1C");
                    // Increment the cursor_index.
                    cursor_index++;
                    continue;
                }
            }
            // Append the current character to the buffer.
            // If the buffer is full, break the loop.
            if (readline_append(filename[i])) {
                break;
            }
        }
        // If the suggestion is a directory, append a trailing slash.
        if ((filetype == DT_DIR) && (line.buffer[cursor_index - 1] != '/')) {
            // Append the slash character to indicate a directory.
            readline_append('/');
        }
    }
}

/// @brief Completes the current line based on cursor_index and length.
/// @return Returns 0 on success, 1 on failure.
static int readline_complete(void)
{
    char cwd[PATH_MAX]; // Buffer to store the current working directory.
    int words;          // Variable to store the word count.
    dirent_t dent;      // Prepare the dirent variable.

    // Count the number of words in the command buffer.
    words = __count_words(line.buffer);

    // If there are no words in the command buffer, log it and return.
    if (words == 0) {
        return 0;
    }

    // Determines if we are at the beginning of a new argument, last character is space.
    if (__is_separator(line.buffer[cursor_index - 1])) {
        return 0;
    }

    // If the last two characters are two dots `..` append a slash `/`, and
    // continue.
    if ((cursor_index >= 2) && ((line.buffer[cursor_index - 1] == '.') && (line.buffer[cursor_index - 2] == '.'))) {
        if (readline_append('/')) {
            fprintf(stderr, "Failed to append character.\n");
            return 1;
        }
    }

    // Attempt to retrieve the current working directory.
    if (getcwd(cwd, PATH_MAX) == NULL) {
        // Error handling: If getcwd fails, it returns NULL
        fprintf(stderr, "Failed to get current working directory.\n");
        return 1;
    }

    // Determines if we are executing a command from current directory.
    int is_run_cmd = (cursor_index >= 2) && (line.buffer[0] == '.') && (line.buffer[1] == '/');
    // Determines if we are entering an absolute path.
    int is_abs_path = (cursor_index >= 1) && (line.buffer[0] == '/');

    // If there is only one word, we are searching for a command.
    if (is_run_cmd) {
        if (__folder_contains(cwd, line.buffer + 2, 0, &dent)) {
            readline_suggest(
                dent.d_name,
                dent.d_type,
                cursor_index - 2);
        }
    } else if (is_abs_path) {
        char _dirname[PATH_MAX];
        if (!dirname(line.buffer, _dirname, sizeof(_dirname))) {
            return 0;
        }
        const char *_basename = basename(line.buffer);
        if (!_basename) {
            return 0;
        }
        if ((*_dirname == 0) || (*_basename == 0)) {
            return 0;
        }
        if (__folder_contains(_dirname, _basename, 0, &dent)) {
            readline_suggest(
                dent.d_name,
                dent.d_type,
                strlen(_basename));
        }
    } else if (words == 1) {
        if (__search_in_path(line.buffer, &dent)) {
            readline_suggest(
                dent.d_name,
                dent.d_type,
                cursor_index);
        }
    } else {
        // Search the last occurrence of a space, from there on
        // we will have the last argument.
        char *last_argument = strrchr(line.buffer, ' ');
        // We need to move ahead of one character if we found the space.
        last_argument = last_argument ? last_argument + 1 : NULL;
        // If there is no last argument.
        if (last_argument == NULL) {
            fprintf(stderr, "readline_complete: No last argument found in buffer '%s'.\n", line.buffer);
            return 0;
        }
        char _dirname[PATH_MAX];
        if (!dirname(last_argument, _dirname, sizeof(_dirname))) {
            fprintf(stderr, "readline_complete: Failed to extract directory name from '%s'.\n", last_argument);
            return 0;
        }
        const char *_basename = basename(last_argument);
        if (!_basename) {
            fprintf(stderr, "readline_complete: Failed to extract basename from '%s'.\n", last_argument);
            return 0;
        }
        if ((*_dirname != 0) && (*_basename != 0)) {
            if (__folder_contains(_dirname, _basename, 0, &dent)) {
                readline_suggest(
                    dent.d_name,
                    dent.d_type,
                    strlen(_basename));
            }
        } else if (*_basename != 0) {
            if (__folder_contains(cwd, _basename, 0, &dent)) {
                readline_suggest(
                    dent.d_name,
                    dent.d_type,
                    strlen(_basename));
            }
        }
    }
    return 0;
}


/// @brief Reads user input into a buffer, supporting basic editing features.
/// @return The user input allocared with malloc(3); It must be freed by the
///         caller.
char* readline(const char* prompt)
{
    int c;
    bool_t insert_active = false;

    if (prompt) {
        printf("%s", prompt);
    }

    // Get terminal attributes for input handling
    struct termios _termios;
    tcgetattr(STDIN_FILENO, &_termios);
    _termios.c_lflag &= ~(ICANON | ECHO | ISIG); // Disable canonical mode and echo
    tcsetattr(STDIN_FILENO, 0, &_termios);       // Set modified attributes

    // Clear the buffer at the start
    rb_history_init_entry(&line);
    cursor_index = 0, length = 0;

    do {
        // Read a character from input.
        c = getchar();

        // Finish reading input if EOF (Ctrl+D) was read.
        if (c == 4) {
            if (strlen(line.buffer)) {
                putchar('\n'); // Display a newline
            }
            break;
        }

        // Handle newline character to finish input
        if (c == '\n') {
            putchar('\n'); // Display a newline
            break;
        }

        // Handle backspace for deletion
        if (c == '\b') {
            if (cursor_index > 0) {
                --length; // Decrease length
                --cursor_index;  // Move cursor_index back
                // Shift the buffer left to remove the character
                memmove(line.buffer + cursor_index,
                        line.buffer + cursor_index + 1,
                        length - cursor_index + 1);
                // Show backspace action.
                putchar('\b');
            }
            continue;
        }

        if (c == '\t') {
            readline_complete();
            continue;
        }

        // Ctrl+C
        if (c == 0x03) {
            // Print a newline to indicate that Ctrl+C was pressed.
            putchar('\n');
            // Return NULL to indicate the Ctrl+C operation.
            return NULL;
        }

        // CTRL+U
        if (c == 0x15) {
            // Clear the current command.
            readline_clear();
            continue;
        }

        if (c == '\033') {
            c = getchar();
            if (c == '[') {
                c = getchar();
                // UP/DOWN ARROW
                if ((c == 'A') || (c == 'B')) {
                    // Clear the current command.
                    readline_clear();
                    // Fetch the history element.
                    rb_history_entry_t *history_entry = history_fetch(c);
                    if (history_entry) {
                        // Sets the command.
                        rb_history_entry_copy(line.buffer, history_entry->buffer, line.size);
                        // Print the old command.
                        printf(line.buffer);
                        // Set cursor_index to the end.
                        cursor_index = length = strnlen(line.buffer, line.size);
                    }
                }
                // LEFT ARROW
                else if (c == 'D') {
                    if (cursor_index > 0) {
                        puts("\033[1D"); // Move the cursor left
                        cursor_index--;         // Decrease cursor_index
                    }
                }
                // RIGHT ARROW
                else if (c == 'C') {
                    if (cursor_index < length) {
                        puts("\033[1C"); // Move the cursor right
                        cursor_index++;         // Increase cursor_index
                    }
                }
                // HOME
                else if (c == 'H') {
                    printf("\033[%dD", cursor_index); // Move cursor to the beginning
                    cursor_index = 0;                 // Set cursor_index to the start
                }
                // END
                else if (c == 'F') {
                    printf("\033[%dC", length - cursor_index); // Move cursor to the end
                    cursor_index = length;                     // Set cursor_index to the end
                }
                // INSERT
                else if (c == '2') {
                    if (getchar() == '~') {
                        // Toggle insert mode.
                        insert_active = !insert_active;
                        if (insert_active) {
                            // Change cursor to an underline cursor.
                            printf("\033[3 q");
                        } else {
                            // Change cursor back to a block cursor (default).
                            printf("\033[0 q");
                        }
                    }
                }
                // DELETE
                else if (c == '3') {
                    if (getchar() == '~') {
                        if (cursor_index < length) {
                            --length;     // Decrease length
                            putchar(127); // Show delete character.
                            // Shift left to remove character at cursor_index
                            memmove(line.buffer + cursor_index, line.buffer + cursor_index + 1, length - cursor_index + 1);
                        }
                    }
                }
                // PAGE_UP
                else if (c == '5') {
                    if (getchar() == '~') {
                        printf("\033[25S");
                    }
                }
                // PAGE_DOWN
                else if (c == '6') {
                    if (getchar() == '~') {
                        printf("\033[25T");
                    }
                }
                // CTRL+ARROW
                else if (c == '1') {
                    c = getchar();
                    if (c == ';') {
                        c = getchar();
                        if (c == '5') {
                            c              = getchar();
                            int move_count = 0;

                            // CTRL+RIGHT ARROW
                            if (c == 'C') {
                                // Move to the beginning of the next word
                                // Skip spaces first
                                while (cursor_index < length && line.buffer[cursor_index] == ' ') {
                                    cursor_index++;
                                    move_count++;
                                }
                                // Move to the end of the current word (non-space characters)
                                while (cursor_index < length && line.buffer[cursor_index] != ' ') {
                                    cursor_index++;
                                    move_count++;
                                }
                                // Apply all movements to the right in one go.
                                if (move_count > 0) {
                                    printf("\033[%dC", move_count);
                                }
                            }
                            // CTRL+LEFTY ARROW
                            else if (c == 'D') {
                                // Move left past spaces first
                                while (cursor_index > 0 && line.buffer[cursor_index - 1] == ' ') {
                                    cursor_index--;
                                    move_count++;
                                }
                                // Move left to the beginning of the current word (non-space characters)
                                while (cursor_index > 0 && line.buffer[cursor_index - 1] != ' ') {
                                    cursor_index--;
                                    move_count++;
                                }
                                // Apply all movements to the left in one go.
                                if (move_count > 0) {
                                    printf("\033[%dD", move_count);
                                }
                            }
                        }
                    }
                }
            }
            continue;
        }

        // Handle insertion based on insert mode.
        if (insert_active) {
            // Move cursor right.
            puts("\033[1C");
            // Prepare to delete the character.
            putchar('\b');
        } else if (cursor_index < length - 1) {
            // Shift buffer to the right to insert new character.
            memmove(line.buffer + cursor_index + 1, line.buffer + cursor_index, length - cursor_index + 1);
        }

        // Append the character.
        if (readline_append(c)) {
            break; // Exit loop if buffer is full.
        }

        // In insert mode, the length stays the same, unless we are at the end of the line.
        if (insert_active && (cursor_index < length)) {
            --length;
        }

    } while (length < line.size);

    if (use_history) {
        history_push();
    }

    tcgetattr(STDIN_FILENO, &_termios);
    _termios.c_lflag |= (ICANON | ECHO | ISIG); // Re-enable canonical mode and echo
    tcsetattr(STDIN_FILENO, 0, &_termios);

    return strdup(line.buffer);
}
