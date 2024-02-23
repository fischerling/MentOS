/// @file logo.c
/// @brief
/// @copyright (c) 2014-2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <stdio.h>
#include <string.h>
#include <time.h>

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--sos") == 0) {
        printf(
"              ____     ___    ____  \n"
"             / ___|   / _ \\  / ___| \n"
"             \\___ \\  | | | | \\___ \\ \n"
"              ___) | | |_| |  ___) |\n"
"             |____/   \\___/  |____/ \n");
    } else {
        printf(
"                   __  __                  _      ___    ____  \n"
"                  |  \\/  |   ___   _ __   | |_   / _ \\  / ___| \n"
"                  | |\\/| |  / _ \\ | '_ \\  | __| | | | | \\___ \\ \n"
"                  | |  | | |  __/ | | | | | |_  | |_| |  ___) |\n"
"                  |_|  |_|  \\___| |_| |_|  \\__|  \\___/  |____/ \n");
    }
    return 0;
}
