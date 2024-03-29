/// @file readpasswd.h
/// @brief Get a passphrase from the user
/// @copyright (c) 2024 Florian Fischer
/// This file is distributed under the MIT License.
/// See LICENSE.md for details.
///
/// This function is inspired by the BSD readpassphrase library function.

#pragma once

#include <stddef.h>

#define RPWD_ECHO_ON 1 << 2

/// @brief  Read a passphrase from the terminal an return it.
/// @param  prompt the prompt to display.
/// @param  buf the provided bufffer to read the password into.
/// @param  bufsiz the size of the provided bufffer.
/// @param  flags the used flags.
/// @return On success a pointer to the null-terminated passphrase.
///         NULL on failure.
char* readpasswd(const char *prompt, char *buf, size_t bufsiz, int flags);
