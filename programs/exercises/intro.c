/// @file intro.c
/// @brief Interactive intro to SOS
/// @copyright (c) 2024 Florian Fischer
/// This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <io/ansi_colors.h>
#include <libgen.h>
#include <readline.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define R(s) FG_RED s FG_WHITE
#define WB(s) FG_WHITE_BRIGHT s FG_WHITE

/*
 * Steps:
 *  0. Intro/Programme ausfuehren
 *  1. Hilfe/man
 *  2. Dateien auflisten
 *  3. CWD
 *  4. Dateien lesen
 *  5. Dateien kopieren
 *  6. Dateien loeschen
 */

static const char* STEP_FILE_DIR = "/var/lib/intro";

static int step = 0;
static char step_file_path[PATH_MAX];
static int step_file_fd;

typedef void (*step_func_t)(void);
typedef int (*next_step_func_t)(void);

static void step0(void) {
    printf(
"Dieses Programm ist dazu gedacht, Ihnen SOS sowie die Benutzung der\n"
"Kommandozeile etwas naeherzubringen.\n"
"\n"
"Um SOS zu benutzen muessen, Sie zuerst lernen wie man Programme ausfuehrt.\n"
"Dazu tippen Sie einfach den Namen des Programms, das Sie ausfuehren moechten,\n"
"gefolgt von den Argumenten, die dem Programm uebergeben werden sollen, ein\n"
"und druecken anschliessend die Enter-Taste.\n"
"\n"
"Probieren wir es gleich aus!\n"
"Tippen Sie "WB("\"intro next\"")" fuer den naechsten Schritt.\n"
    );
}

static int next_step0(void) {
    printf(
"Sehr gut! Sie haben erfolgreich ein Programm mit Argumenten ausgefuehrt.\n"
"Alles was die " R("shell") ", das Programm in dem Sie sich gerade befinden,\n"
"macht, ist jede Eingabezeile in einzelne Woerter zu zerlegen.\n"
"Das erste Wort ist das Programm, das ausgefuehrt werden soll.\n"
"Die restlichen Woerter der Kommandozeile werden dem neue Programm uebergeben.\n"
"Sie haben das Programm "WB("intro")" mit dem Argument "WB("next")" aufgerufen.\n"
"Machen Sie das Gleiche nochmal fuer den naechsten Schritt.\n"
"Sie koennen das Programm "WB("intro")" jeder Zeit wieder aufrufen,\n"
"um den aktuellen Schritt erneut zu lesen.\n"
    );
    return true;
}

static void step1(void) {
    printf(
"Eine genauere Beschreibung, wie das Betriebssystem Programme ausfuehrt,\n"
"koennen Sie in "WB("Abschnitt 1.1")" des Arbeitsheftes nachlesen.\n"
"Nachdem Sie nun Progamme ausfuehren koennen, waere es nuetzlich zu wissen,\n"
"welche Programme es gibt und wie man sie benutzt.\n"
"Das Programm " WB("man") " erlaubt es Ihnen die Dokumentation (engl. " R("man") "ual) des Systems  zu lesen.  "
"Tippen Sie \""WB("man")"\" und "WB("<Enter>")" um eine Liste aller Dokumentationsseiten zu erhalten.\n"
"Um eine spezifische Dokumentationsseite zu lesen, fuehren Sie das Programm " WB("man") "  aus und uebergeben den Namen der Seite als erstes Argument.\n"
WB("Beispiel: man man") " - zeigt die Dokumentation zu dem Programm man.\n"
WB("Tipp:") " Viele Programme unterstuetzen auch ein --help Argument.\n"
    );
}

static int next_step1(void) {
    char *answer = readline("Aus welcher Datei liest die shell Befehle, bevor sie Nutzereingaben verarbeitet?\n> ");
    if (strstr(answer, "shellrc") != NULL) {
    printf(
"Korrekt! Alle Befehle in der Datei .shellrc werden nach dem Einloggen ausgefuehrt.\n"
    );
        return true;
    }

    if (strstr(answer, "shell") == NULL)
        printf("Leider nein. "WB("Tipp:")" Lesen Sie die Dokumentationsseite zu dem Programm shell.\n");
    else
        printf("Fast. "WB("Tipp:")" Lesen Sie die Dokumentationsseite zu shell "R("genau")".\n");

    return false;
}

static void step2(void) {
    printf(
"Alle Dateien eines Verzeichnisses (engl. directory) kann man mit dem Programm " WB("ls") " (engl. " R("l") "i" R("s") "t) anzeigen lassen.  "
"Wird kein Pfad zu einem Verzeichnis angegeben,  werden die Dateien aus dem " WB("aktuellen Verzeichnis") " aufgelistet.\n"
"Eine Aufgabe des Betriebssystems ist die Verwaltung von Dateisystemen.\n"
"Anders als in Windows existiert in SOS nur ein Ursprung "R("'/'") ", das Wurzel-Verzeichnis (engl. " WB("root") ").  "
"Pfade zu Verzeichnissen oder Dateien koennen ausgehend von   diesem Verzeichnis angegeben werden.\n"
WB("Beispiel:")"\n\t'/home/alice' ist der Pfad zu Alice Home-Verzeichnis, der Ort aller Dateien von Alice.\n"
"Pfade, die mit dem Wurzel-Verzeichnis '/' beginnen, nennt man " R("absolute") " Pfade.\n"
    );
}

static int next_step2(void) {
    char *answer = readline("Welche Datei im aktuellen Verzeichnis beginnt mit 'R'?\n> ");
    if (strstr(answer, "README") != NULL) {
        printf("Genau! Der Name ist uebrigens eine Aufforderung.\n");
        return true;
    }

    printf(
"Leider falsch. "WB("Tipp:")" Benutzen Sie das Programm "WB("ls")" um sich alle Dateien auflisten zu lassen.\n"
    );
    return false;
}

static void step3(void) {
    printf(
"Das Betriebssystem merkt sich, in welchem Verzeichnis ein Programm ausgefuehrt    wird (engl. " WB("current working directory") " kurz " WB("CWD") ").\n"
"Das CWD wird von dem Prozess, der das neue Programm startet, \"geerbt\".\n"
"In der shell koennen Sie interaktiv das aktuelle Verzeichnis mit dem Befehl " WB("cd") "  (engl. " R("c") "hange " R("d") "irectory) aendern.\n"
"Die shell zeigt in jeder Zeile das aktuelle Verzeichnis in eckigen Klammern an.\n"
"Wechslen Sie einige Male das Verzeichnis mit dem " WB("cd") " Befehl.\n"
WB("Tipp:") " Das Programm " WB("pwd") ", (engl. " R("p") "rint " R("w") "orking " R("d") "irectory) zeigt den absoluten Pfad des CWD an.\n"
    );
}

static int next_step3(void) {
    char *answer = readline("Welches Symbol zeigt die shell in eckigen Klammern im Verzeichnis /home/alice?\n> ");
    if (answer[0] != '~') {
    printf(
"Nein. " WB("Tipp:")" Lesen Sie in der Dokumentationsseite zu "WB("cd")", wie Sie ins Home-Verzeichnis gelangen.\n");
        return false;
    }

    printf(
"Richtig! Das Symbol '"WB("~")"', steht fuer das Home-Vezeichnis des aktuell angemeldeten Benutzers.\n"
    );
    return true;
}

static void step4(void) {
    printf(
"Um an den Inhalt einer Datei zu gelangen, stehen mehrere Programme zur\nVerfuegung.\n"
"Das Program " WB("cat") " (engl. conc" R("cat") "inate) beispielsweise gibt den Inhalt einer oder  mehrerer Dateien zusammenhaengend aus.\n"
WB("head") " kann verwendet werden, um nur die ersten Zeilen von Dateien anzeigen zu    lassen.  "
"Ist eine Datei zu lang, um auf den Bildschirm angezeigt zu werden, kann " WB("more") " verwendet werden, um die Datei Zeile fuer Zeile zu lesen.\n"
"Um gezielt nach Woertern zu suchen, steht das Programm " WB("fgrep") " zur Verfuegung.\n"
    );
}

static int next_step4(void) {
    char *answer = readline("Wie lautet das erste Wort in der dritten Zeile der Datei " WB("/etc/passwd") " ?\n> ");
    if (strcmp(answer , "bob") != 0) {
    printf("Das ist so nicht richtig.\n");
        return false;
    }
    printf(
"Korrekt! In der Datei /etc/passwd werden alle Benutzerzugaenge des Systems aufgelistet.\n"
"Mehr Informationen koennen Sie in der Dokumentationsseite zu "WB("passwd")" nachlesen.\n"
    );
    return true;
}

static void step5(void) {
    printf(
"Wird eine Datei mehrmals benoetigt, kann sie mit dem Programm " WB("cp") " (engl. " R("c") "o" R("p") "y) an einen neuen Ort bzw. in eine Datei mit anderem Namen kopiert werden.\n" 
"In der Angabe von Pfaden koennen die besonderen Bezeichner "WB("\".\"")" und "WB("\"..\"")"\nverwendet werden.  "
"Besonders um " R("relative") " Pfade, also Pfade ausgehend vom aktuellen Verzeichnis (CWD) anzugeben, koennen diese nuetzlich sein. "
WB("\".\"") " steht dabei fuer das Verzeichnis selbst und " WB("\"..\"") " bezeichnet das Oberverzeichnis.\n"
WB("Beispiel") ": CWD=/home/alice\n"
"\t\""WB(".")"\" => /home/alice\n"
"\t\""WB("..")"\" => /home\n"
"\""WB("../bob")"\" => /home/bob\n"
    );
}

static int next_step5(void) {
    char * answer = readline("Wie lautet der Befehl, um die Datei namens \"foo\" in die Datei \"bar\"\nim Oberverzeichnis zu kopieren?\n> "
    );
    if (strcmp(answer , "cp foo ../bar") != 0) {
        if (!strstr(answer, "../"))
            printf("Leider falsch.\nDen Bezeichner fuer das Oberverzeichnis nicht vergessen.\n");
        else
            printf("Leider falsch.\n");
        return false;
    }
    printf("Korrekt! Sehr schoen.\n");
    return true;
}

static void step6(void) {
    printf(
"Wurde eine Datei versehentlich kopiert oder wird nicht mehr benoetigt, kann sie mithilfe des Programms " WB("rm") " (engl. " R("r") "e" R("m") "ove) entfernt werden.\n"
"Leere Verzeichnisse lassen sich mit dem Programm " WB("rmdir") " (engl. " R("r") "e" R("m") "ove " R("dir")"ectory) entfernen.\n"
    );
}

static int next_step6(void) {
    char *answer = readline("Wie lautet der Befehl, um die Datei \"todo\" des Nutzers bob aus dessen\nHome-Verzeichnis, unabhaengig vom aktuellen Verzeichnis zu entfernen?\n> ");
    if (strcmp(answer , "rm /home/bob/todo") != 0) {
        if (answer[3] != '/')
            printf("Achten Sie darauf einen absoluten Pfad anzugeben.\n");
        else if (!strstr(answer, "home"))
            printf("Home-Verzeichnisse befinden sich unter /home/");
        else if (!strstr(answer, "bob"))
            printf("Die Datei soll aus bobs Home-Verzeichnis geloescht werden.\n");
        return false;
    }
    printf("Stimmt genau!\n");
    return true;
}

static void step7(void) {
    printf(
"Die shell erlaubt es mit dem "WB("\">\"")"-Operator die Ausgabe eines Programms in eine Datei umzuleiten.  "
"Jedem Programm stellt das Betriebssystem zwei Ausgabe-Kanaele  zur Verfuegung, die normalerweise einfach auf dem Bildschirm erscheinen.  "
/* "Diese Kanaele koennen fuer unterschiedliche Ausgaben verwendet werden." */
"Sie werden " WB("stdout") " ("R("st") "an" R("d") "art " R("out") "put) und " WB("stderr") " ("R("st") "an"R("d")"art "R("err")"or) genannt.\n"
WB("Beispiele")": Um stdout des Progamms ls in die Datei \"datei-liste.txt\" umzuleiten, kann der Befehl \"" WB("ls > datei-liste.text")"\" verwendet werden.\n"
"Um nur die Fehler des Programms rm in die Datei \"remove-errors.txt\" umzuleiten, kann der Befehl "WB("\"rm foo bar 2> remove-error.txt\"") " verwendet werden.\n"
"Um beide Kanaele eines Programms umzuleiten, kann der Befehl "WB("\"programm &> ausgaben.txt\"")" verwendet werden.\n"
"Erstelle die Datei \"/home/alice/hello.txt\", die nur das Wort \"hello\" enthaelt mithilfe des " WB("echo") " Programms.\n"
    );
}

static int next_step7(void) {
    int fd = open("/home/alice/hello.txt", O_RDONLY, 0);
    if (fd < 0) {
        printf("Die Datei /home/alice/hello.txt existiert noch nicht.");
        return false;
    }
    char buf[5];
    ssize_t r = read(fd, buf, sizeof(buf));
    if (r != 5) {
        if (r < 0)
            perror("Fehler beim Lesen der Datei");
        else
            printf("Die Datei enthaelt zu wenig Text.\n");
        return false;
    }

    if (memcmp(buf, "hello", 5) != 0) {
        printf("Die Datei enthaelt nicht den Text \"hello\".\n");
        return false;
    }
    printf("Perfekt. Sie sind bereit!\n");
    return true;
}


static step_func_t steps[] = {
    &step0,
    &step1,
    &step2,
    &step3,
    &step4,
    &step5,
    &step6,
    &step7,
};

static next_step_func_t next_step[] = {
    &next_step0,
    &next_step1,
    &next_step2,
    &next_step3,
    &next_step4,
    &next_step5,
    &next_step6,
    &next_step7,
};

enum cmd {
    PRINT_STEP,
    NEXT_STEP,
    RESET,
};

static void print_help(void) {
    printf(
"Interaktive SOS Einfuehrung\n"
"Usage: intro [next|reset|help]\n"
"\n"
"  next   Beginne den naechsten Schritt der Einfuehrung\n"
"  reset  Setze die Einfuehrung zurueck\n"
"  help   Zeige diese Nachricht\n"
"\n"
"Falls kein Argument angeben wurde, wird der aktuelle Schritt wiederholt.\n"
);
}

int main(int argc, char **argv)
{
    puts(FG_WHITE);
    int cmd = PRINT_STEP;
    if (argc == 2) {
        if (strcmp(argv[1], "next") == 0)
            cmd = NEXT_STEP;
        else if (strcmp(argv[1], "reset") == 0)
            cmd = RESET;
        else if (strcmp(argv[1], "help") == 0) {
            print_help();
            exit(EXIT_SUCCESS);
        }
        else
            errx(EXIT_FAILURE, "Usage: %s [next|reset|help]", argv[0]);
    }

    uid_t uid = getuid();
    sprintf(step_file_path, "%s/%d/step", STEP_FILE_DIR, uid);

    if (cmd == RESET) {
        int ret = unlink(step_file_path);
        if (ret < 0)
            err(EXIT_FAILURE, "removing step file");
        exit(EXIT_SUCCESS);
    }

    // cmd is either PRINT_STEP or NEXT_STEP.
    // First determine the current step.
    step_file_fd = open(step_file_path, O_RDONLY, 0);
    if (step_file_fd < 0) {
        // Create the user intro directory
        char user_dir_path[PATH_MAX];
        dirname(step_file_path, user_dir_path, sizeof(user_dir_path));
        if (mkdir(user_dir_path, 0770) == -1 && errno != EEXIST)
            err(EXIT_FAILURE, "create user intro directory");

        // Create the step file
        step_file_fd = creat(step_file_path, 0660);
        if (step_file_fd < 0)
            err(EXIT_FAILURE, "create step file");
        fprintf(step_file_fd, "%d\n", step);
    } else {
        char step_buf[2];
        if (read(step_file_fd, step_buf, 2) != 2)
            err(EXIT_FAILURE, "read step file");
        close(step_file_fd);

        if (step_buf[1] == '\n')
            step_buf[1] = 0;

        step = atoi(step_buf);
    }

    if (cmd == NEXT_STEP && next_step[step]()) {
        step++;
        step_file_fd = open(step_file_path, O_WRONLY, 0);
        if (step_file_fd < 0)
            err(EXIT_FAILURE, "open step file for writing");
        fprintf(step_file_fd, "%d\n", step);
    }

    if (step == 0)
        printf("Willkommen in der Einfuehrung zu SOS, dem School Operating System.\n\n");
    else if (step >= sizeof(steps) / sizeof(step_func_t)) {
        printf(
"Herzlichen Glueckwunsch, Sie haben den letzten Einfuehrungschritt erreicht.\n"
WB("intro reset") " setzt die Einfuehrung zurueck.\n"
"Weitere Aufgaben stehen unter /usr/bin/exercises/ zur Verfuegung.\n"
);
        return 0;
    }

    printf(FG_BLUE_BRIGHT "\nSchritt: %d\n" FG_WHITE, step);
    steps[step]();
    close(step_file_fd);
    return 0;
}
