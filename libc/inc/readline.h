/// @file readline.h
/// @brief Get a line from a user with editing
/// @copyright (c) 2024 Florian Fischer
/// This file is distributed under the MIT License.
/// See LICENSE.md for details.

#pragma once

/// @brief  Read a line from the terminal an return it.
/// @param  prompt the prompt to issue.
/// @return The line inserted by the user. The line is allocated using malloc
///         and must be freed. The final new line is stripped from the string.
char* readline(const char *prompt);

/// @brief  Use the readline history
void using_history(void);

/// @brief  Function pointer used to complete words
void (*readline_complete_func)(void);

