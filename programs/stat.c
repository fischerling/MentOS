/// @file stat.c
/// @brief display file status
/// @copyright (c) 2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

#include <fcntl.h>
#include <io/debug.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <strerror.h>
#include <string.h>
#include <sys/bitops.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/unistd.h>

static void __print_time(const char *prefix, time_t *time)
{
    tm_t *timeinfo = localtime(time);
    printf("%s%d-%d-%d %d:%d:%d\n",
           prefix,
           timeinfo->tm_year,
           timeinfo->tm_mon,
           timeinfo->tm_mday,
           timeinfo->tm_hour,
           timeinfo->tm_min,
           timeinfo->tm_sec);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("%s: missing operand.\n", argv[0]);
        printf("Try '%s --help' for more information.\n", argv[0]);
        exit(1);
    }
    if (strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s FILE\n", argv[0]);
        printf("Display file status.\n");
        exit(0);
    }
    stat_t dstat;
    if (stat(argv[1], &dstat) == -1) {
        printf("%s: cannot stat '%s': %s\n", argv[0], argv[1], strerror(errno));
        exit(1);
    }

    printf("File: %s", argv[1]);
    if (S_ISLNK(dstat.st_mode)) {
        char link_buffer[PATH_MAX];
        ssize_t len;
        if ((len = readlink(argv[1], link_buffer, sizeof(link_buffer))) != -1) {
            link_buffer[len] = '\0';
            printf(" -> %s", link_buffer);
        }
    }
    putchar('\n');
    printf("Size: %s\n", to_human_size(dstat.st_size));
    printf("File type: ");
    switch (dstat.st_mode & S_IFMT) {
    case S_IFBLK : printf("block device\n"); break;
    case S_IFCHR : printf("character device\n"); break;
    case S_IFDIR : printf("directory\n"); break;
    case S_IFIFO : printf("fifo/pipe\n"); break;
    case S_IFLNK : printf("symbolic link\n"); break;
    case S_IFREG : printf("regular file\n"); break;
    case S_IFSOCK: printf("socket\n"); break;
    default      : printf("unknown?\n"); break;
    }

    // Build the access permissions string.
    char mode[] = "----------";
    switch (dstat.st_mode & S_IFMT) {
    case S_IFBLK : mode[0] = 'b'; break;
    case S_IFCHR : mode[0] = 'v'; break;
    case S_IFDIR : mode[0] = 'f'; break;
    case S_IFIFO : mode[0] = 'p'; break;
    case S_IFLNK : mode[0] = 'l'; break;
    case S_IFREG : mode[0] = '-'; break;
    case S_IFSOCK: mode[0] = 's'; break;
    default      : mode[0] = '?'; break;
    }

    if (dstat.st_mode & S_IRUSR) mode[1] = 'r';
    if (dstat.st_mode & S_IWUSR) mode[2] = 'w';
    if (dstat.st_mode & S_IXUSR) mode[3] = 'x';
    if (dstat.st_mode & S_IRGRP) mode[4] = 'r';
    if (dstat.st_mode & S_IWGRP) mode[5] = 'w';
    if (dstat.st_mode & S_IXGRP) mode[6] = 'x';
    if (dstat.st_mode & S_IROTH) mode[7] = 'r';
    if (dstat.st_mode & S_IWOTH) mode[8] = 'w';
    if (dstat.st_mode & S_IXOTH) mode[9] = 'x';

    if (dstat.st_mode & S_ISUID) mode[3] = (mode[3] == 'x') ? 's' : 'S';
    if (dstat.st_mode & S_ISGID) mode[6] = (mode[6] == 'x') ? 's' : 'S';
    if (dstat.st_mode & S_ISVTX) mode[9] = (mode[9] == 'x') ? 't' : 'T';

    printf("Access: (%.4o/%s)", dstat.st_mode & 0xFFF, mode);

    passwd_t *user = getpwuid(dstat.st_uid);
    if (!user) {
        printf("%s: failed to retrieve uid '%u'.\n", argv[0], dstat.st_uid);
        exit(1);
    }
    group_t *group = getgrgid(dstat.st_gid);
    if (!group) {
        printf("%s: failed to retrieve gid '%u'.\n", argv[0], dstat.st_gid);
        exit(1);
    }
    printf(" Uid: (%d/%s) Gid: (%d/%s)\n", dstat.st_uid, user->pw_name, dstat.st_gid, group->gr_name);
    __print_time("Access: ", &dstat.st_atime);
    __print_time("Modify: ", &dstat.st_mtime);
    __print_time("Change: ", &dstat.st_ctime);
    return 0;
}
