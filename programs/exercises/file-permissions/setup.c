/// @file file-permissions/setup.c
/// @brief Setup the file-permissions exercise
/// @copyright (c) 2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>

static char *alice_secret = "Super geheimer Text hier!\n";
static char *bobs_secret = "Arg! Aber mein super sicheres Geheimnis in "
                           "'top_secret.txt' findest du nicht raus!\n";
static char *bobs_top_secret = "1337\n";

int main(int argc, char **argv)
{
    if (geteuid() != 0) {
        fprintf(STDERR_FILENO, "not running as root\n");
        exit(EXIT_FAILURE);
    }

    // Set dangerously permissive permissions for the home directories
    chmod("/home/alice", 777);
    // Bob does not allow others to read his home directory
    chmod("/home/bob", 773);

    int fd;
    int creat_flags = O_CREAT | O_TRUNC | O_WRONLY;
    // World readable and writable
    mode_t insecure_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    // Only user readable and writable
    mode_t secure_mode = S_IRUSR | S_IWUSR;
    // Create the secret files
    fd = open("/home/alice/secrets.txt", creat_flags, insecure_mode);
    write(fd, alice_secret, strlen(alice_secret));
    close(fd);
    // Apparently, bob owns alice secrets.txt file
    chown("/home/alice/secrets.txt", 1001, 1001);

    fd = open("/home/bob/secrets.txt", creat_flags, insecure_mode);
    write(fd, bobs_secret, strlen(bobs_secret));
    close(fd);
    chown("/home/bob/secrets.txt", 1001, 1001);

    fd = open("/home/bob/top_secret.txt", creat_flags, secure_mode);
    write(fd, bobs_top_secret, strlen(bobs_top_secret));
    close(fd);
    chown("/home/bob/top_secret.txt", 1001, 1001);

    printf(
"In MentOS ist es ueblich, dass Geheimnisse in einer Datei namens\n"
"'secrets.txt' im eigenen Home Verzeichnis (/home/<user>/) \n"
"gespeichert werden.\n"
"Leider wurde nicht auf die Datei-Berechtigungen geachtet\n"
"Schaffst du es, dass deine Geheimnisse sicher sind und\n"
"du alle Geheimnisse von bob erfaehrst?\n\n"
"Hilfreiche Programme: chmod, chown, stat, ls, (echo, cp)\n"
"Überprüfe deinen Fortschritt mit dem checkup Befehl der Aufgabe\n");


    return 0;
}
