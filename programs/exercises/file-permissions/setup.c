/// @file file-permissions/setup.c
/// @brief Setup the file-permissions exercise
/// @copyright (c) 2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <err.h>
#include <fcntl.h>
#include <io/ansi_colors.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ALICE 1000

#define R(s) FG_RED s FG_RESET
#define WB(s) FG_WHITE_BRIGHT s FG_RESET

#define CREAT_FLAGS (O_CREAT | O_TRUNC | O_WRONLY)
// World readable and writable
#define INSECURE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)
// Only user readable and writable
#define SECURE_MODE ( S_IRUSR | S_IWUSR)

#define LANDSCHAFT_PATH "/home/alice/Landschaft"

static void __write_file(const char *filename, int flags, mode_t mode, const char *content) {
    int fd = open(filename, flags, mode);
    if (fd < 0) {
        err(EXIT_FAILURE, "opening %s failed", filename);
    }
    if (write(fd, content, strlen(content)) < 0) {
        err(EXIT_FAILURE, "writing content failed");
    }
    close(fd);
}

static char *alice_secret = "Super geheimer Text hier!\n";

static void prepare_alice_secret(void) {
    // Create the secret file
    __write_file("/home/alice/secrets.txt", CREAT_FLAGS, INSECURE_MODE, alice_secret);
    // Apparently, bob owns alice secrets.txt file
    if (chown("/home/alice/secrets.txt", 1001, 1001) < 0) {
        err(EXIT_FAILURE, "chown alice secrets failed");
    }
}

static char *bobs_secret = "Arg! Aber mein super sicheres Geheimnis in "
                           "'top_secret.txt' finden Sie nicht raus!\n";

static void prepare_bob_secret(void) {
    __write_file("/home/bob/secrets.txt", CREAT_FLAGS, INSECURE_MODE, bobs_secret);
    if (chown("/home/bob/secrets.txt", 1001, 1001) < 0) {
        err(EXIT_FAILURE, "chown bob's secrets failed");
    }
}

static char *bobs_top_secret = "1337\n";

static void prepare_bob_top_secret(void) {
    __write_file("/home/bob/top_secret.txt", CREAT_FLAGS, SECURE_MODE, bobs_top_secret);
    if (chown("/home/bob/top_secret.txt", 1001, 1001) < 0) {
        err(EXIT_FAILURE, "chown bob's top secrets failed");
    }
}

static void __checked_mkdir(const char* pathname, mode_t mode) {
    int ret = mkdir(pathname, mode);
    if (ret) { err(EXIT_FAILURE, "creating %s failed", pathname); }
}

static void __checked_chown(const char* pathname, uid_t owner, gid_t group) {
    int ret = chown(pathname, owner, group);
    if (ret) { err(EXIT_FAILURE, "creating %s failed", pathname); }
}

static void prepare_landscape(void) {
    stat_t st;
    if (stat(LANDSCHAFT_PATH, &st) == 0)
            return;

    // WilderWesten drwxrwxrwx
    __checked_mkdir(LANDSCHAFT_PATH, 0555);
    __checked_chown(LANDSCHAFT_PATH, ALICE, ALICE);

    __checked_mkdir(LANDSCHAFT_PATH "/WilderWesten", 0777);

    // Museum dr-xr-xr-x
    __checked_mkdir(LANDSCHAFT_PATH "/Museum", 0555);

    __write_file(LANDSCHAFT_PATH "/Museum/Schaufel", CREAT_FLAGS, 0555,
                 "#!/bin/shell\necho Diggy Diggy Hole\n");

    __write_file(LANDSCHAFT_PATH "/Museum/Gaestebuch", CREAT_FLAGS, 0666,
                 "Erster!\n1337Hax0r3000 was here\nWer das liest ist clever ;P\n");

    __write_file(LANDSCHAFT_PATH "/Museum/Schaubild", CREAT_FLAGS, 0444,
"Das ist eine Kuh!\n"
"< Muuh! >\n"
" -------\n"
"        \\   ^__^\n"
"         \\  (oo)\\_______\n"
"            (__)\\       )\\/\\\n"
"                ||----w |\n"
"                ||     ||\n"
);

    // Generalschluessel sr-xr-xr-x
    __write_file(LANDSCHAFT_PATH "/Museum/Generalschluessel", CREAT_FLAGS, 06555,
                 "#!/bin/shell\necho Du bist jetzt\nid\nshell\n");

    // Wohnung drwx------
    __checked_mkdir(LANDSCHAFT_PATH "/Wohnung", 0700);
    __checked_chown(LANDSCHAFT_PATH "/Wohnung", ALICE, ALICE);

    // Nebel d--x--x--x
    __checked_mkdir(LANDSCHAFT_PATH "/Nebel", 0111);

    __write_file(LANDSCHAFT_PATH "/Nebel/Korn", CREAT_FLAGS, 0444,
                 "Selbst ein blindes Huhn ...\n");

    // Vereinsheim drwxrwx---
    __checked_mkdir(LANDSCHAFT_PATH "/Vereinsheim", 0570);
    __checked_chown(LANDSCHAFT_PATH "/Vereinsheim", ALICE, 984);

    // Briefkasten drwx-w--w-
    __checked_mkdir(LANDSCHAFT_PATH "/Briefkasten", 0622);
}


int main(int argc, char **argv)
{
    if (geteuid() != 0) {
        errx(EXIT_FAILURE, "not running as root");
    }

    // Set dangerously permissive permissions for the home directories
    if (chmod("/home/alice", 0777) < 0) {
        err(EXIT_FAILURE, "setting alice home permission failed");
    }
    // Bob does not allow others to read his home directory
    if (chmod("/home/bob", 0773) < 0) {
        err(EXIT_FAILURE, "setting alice home permission failed");
    }

    prepare_alice_secret();
    prepare_bob_secret();
    prepare_bob_top_secret();
    prepare_landscape();

    printf(
FG_WHITE"Entdecken Sie die Begeisterung fuer Kartographie in sich und untersuchen\n"
"Sie die Datei-Berechtigungen und ihre Bedeutung im Verzeichnis\n"
WB("/home/alice/Landschaft/")".\n"
"\n"
WB("Herrausforderung:\n")
"In MentOS ist es ueblich, dass Geheimnisse in einer Datei namens\n"
WB("'secrets.txt'")" im eigenen Home Verzeichnis (" WB("/home/<user>/") ") \n"
"gespeichert werden.\n"
"Leider wurde nicht auf die "R("Datei-Berechtigungen")" geachtet.\n"
"Schaffen Sie es, dass Ihre Geheimnisse sicher sind und\n"
"Sie alle Geheimnisse von bob erfaehren?\n\n"
"Hilfreiche Programme: "WB("chmod")", "WB("chown")", "WB("stat")", "WB("ls")", (echo, cp, doas)\n"
WB("Hinweis:")" Befehle in der Datei ~/.shellrc werden beim Login ausgefuehrt.\n"
"Ueberpruefen Sie ihren Fortschritt mit dem "WB("checkup")" Befehl der Aufgabe.\n");

    return 0;
}
