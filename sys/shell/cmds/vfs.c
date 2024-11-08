/*
 * Copyright (C) 2016 Eistec AB
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     sys_shell_commands
 * @{
 *
 * @file
 * @brief       Shell commands for the VFS module
 *
 * @author      Joakim Nohlgård <joakim.nohlgard@eistec.se>
 *
 * @}
 */

#if MODULE_VFS
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "macros/units.h"
#include "shell.h"
#include "tiny_strerror.h"
#include "vfs.h"
#include "vfs_util.h"

#ifndef SHELL_VFS_PATH_SIZE_MAX
#define SHELL_VFS_PATH_SIZE_MAX 256
#endif

#define SHELL_VFS_BUFSIZE 256
static uint8_t _shell_vfs_data_buffer[SHELL_VFS_BUFSIZE];

/**
 * @brief Auto-Mount array
 */
XFA_USE_CONST(vfs_mount_t, vfs_mountpoints_xfa);

/**
 * @brief Number of automatic mountpoints
 */
#define MOUNTPOINTS_NUMOF XFA_LEN(vfs_mount_t, vfs_mountpoints_xfa)

static void _ls_usage(char **argv)
{
    printf("%s <path>\n", argv[0]);
    puts("list files in <path>");
}

static void _vfs_usage(char **argv)
{
    printf("%s r <path> [bytes] [offset]\n", argv[0]);
#ifdef MODULE_XIPFS
    printf("%s w <path> <ascii|hex|b64> <a|o> <data>\n", argv[0]);
#else
    printf("%s w <path> <ascii|hex> <a|o> <data>\n", argv[0]);
#endif /* MODULE_XIPFS */
    printf("%s ls <path>\n", argv[0]);
    printf("%s cp <src> <dest>\n", argv[0]);
    printf("%s mv <src> <dest>\n", argv[0]);
    printf("%s mkdir <path> \n", argv[0]);
    printf("%s rm"
#if IS_USED(MODULE_VFS_UTIL)
               " [-r]"
#endif
           " <path>\n", argv[0]);
    printf("%s df [path]\n", argv[0]);
    if (MOUNTPOINTS_NUMOF > 0) {
        printf("%s mount [path]\n", argv[0]);
    }
    if (MOUNTPOINTS_NUMOF > 0) {
        printf("%s umount [path]\n", argv[0]);
    }
    if (MOUNTPOINTS_NUMOF > 0) {
        printf("%s remount [path]\n", argv[0]);
    }
    if (MOUNTPOINTS_NUMOF > 0) {
        printf("%s format [path]\n", argv[0]);
    }
#ifdef MODULE_XIPFS
    printf("%s mk: <name> <size> <exec>\n", argv[0]);
    printf("%s exec: <file> [arg0] [arg1] ... [argn]\n", argv[0]);
#endif /* MODULE_XIPFS */
    puts("r: Read [bytes] bytes at [offset] in file <path>");
#ifdef MODULE_XIPFS
    puts("w: Write (<a>: append, <o> overwrite) <ascii> or <hex> or <b64> string <data> in file <path>");
#else
    puts("w: Write (<a>: append, <o> overwrite) <ascii> or <hex> string <data> in file <path>");
#endif /* MODULE_XIPFS */
    puts("ls: List files in <path>");
    puts("mv: Move <src> file to <dest>");
    puts("mkdir: Create directory <path> ");
    puts("cp: Copy <src> file to <dest>");
    puts("rm: Unlink (delete) a file or a directory at <path>");
    puts("df: Show file system space utilization stats");
#ifdef MODULE_XIPFS
    puts("mk: allocate the space needed to load a file");
    puts("exec: run a binary");
#endif /* MODULE_XIPFS */
}

static void _print_size(uint64_t size)
{
    unsigned long len;
    const char *unit;

    if (size == 0) {
        len = 0;
        unit = NULL;
    } else if ((size & (GiB(1) - 1)) == 0) {
        len = size / GiB(1);
        unit = "GiB";
    }
    else if ((size & (MiB(1) - 1)) == 0) {
        len = size / MiB(1);
        unit = "MiB";
    }
    else if ((size & (KiB(1) - 1)) == 0) {
        len = size / KiB(1);
        unit = "KiB";
    } else {
        len = size;
        unit = NULL;
    }

    if (unit) {
        printf("%8lu %s ", len, unit);
    } else {
        printf("%10lu B ", len);
    }
}

static void _print_df(vfs_DIR *dir)
{
    struct statvfs buf;
    int res = vfs_dstatvfs(dir, &buf);
    printf("%-16s ", dir->mp->mount_point);
    if (res < 0) {
        printf("statvfs failed: %s\n", tiny_strerror(res));
        return;
    }

    _print_size(buf.f_blocks * buf.f_bsize);
    _print_size((buf.f_blocks - buf.f_bfree) * buf.f_bsize);
    _print_size(buf.f_bavail * buf.f_bsize);
    printf("%7lu%%\n", (unsigned long)(((buf.f_blocks - buf.f_bfree) * 100) / buf.f_blocks));
}

static int _df_handler(int argc, char **argv)
{
    puts("Mountpoint              Total         Used    Available     Use%");
    if (argc > 1) {
        const char *path = argv[1];
        /* Opening a directory just to statfs is somewhat odd, but it is the
         * easiest to support with a single _print_df function */
        vfs_DIR dir;
        int res = vfs_opendir(&dir, path);
        if (res == 0) {
            _print_df(&dir);
            vfs_closedir(&dir);
        } else {
            printf("Failed to open `%s`: %s\n", path, tiny_strerror(res));
        }
    }
    else {
        /* Iterate through all mount points */
        vfs_DIR it = { 0 };
        while (vfs_iterate_mount_dirs(&it)) {
            _print_df(&it);
        }
    }
    return 0;
}

static int _mount_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s [path]\n", argv[0]);
        puts("mount pre-configured mount point");
        return -1;
    }

    int res = vfs_mount_by_path(argv[1]);
    if (res < 0) {
        puts(tiny_strerror(res));
    }

    return res;
}

static int _umount_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s [path]\n", argv[0]);
        puts("umount pre-configured mount point");
        return -1;
    }

    int res = vfs_unmount_by_path(argv[1], false);
    if (res < 0) {
        puts(tiny_strerror(res));
    }

    return res;
}

static int _remount_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s [path]\n", argv[0]);
        puts("remount pre-configured mount point");
        return -1;
    }

    vfs_unmount_by_path(argv[1], false);
    int res = vfs_mount_by_path(argv[1]);
    if (res < 0) {
        puts(tiny_strerror(res));
    }

    return res;
}

static int _format_handler(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: %s [path]\n", argv[0]);
        puts("format pre-configured mount point");
        return -1;
    }

    int res = vfs_format_by_path(argv[1]);
    if (res < 0) {
        puts(tiny_strerror(res));
    }

    return res;
}

static int _read_handler(int argc, char **argv)
{
    uint8_t buf[16];
    size_t nbytes = sizeof(buf);
    off_t offset = 0;
    char *path = argv[1];
    if (argc < 2) {
        puts("vfs read: missing file name");
        return 1;
    }
    if (argc > 2) {
        nbytes = atoi(argv[2]);
    }
    if (argc > 3) {
        offset = atoi(argv[3]);
    }

    int res;
    res = vfs_normalize_path(path, path, strlen(path) + 1);
    if (res < 0) {
        printf("Invalid path \"%s\": %s\n", path, tiny_strerror(res));
        return 5;
    }

    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) {
        printf("Error opening file \"%s\": %s\n", path, tiny_strerror(fd));
        return 3;
    }

    res = vfs_lseek(fd, offset, SEEK_SET);
    if (res < 0) {
        printf("Seek error: %s\n", tiny_strerror(res));
        vfs_close(fd);
        return 4;
    }

    while (nbytes > 0) {
        memset(buf, 0, sizeof(buf));
        size_t line_len = (nbytes < sizeof(buf) ? nbytes : sizeof(buf));
        res = vfs_read(fd, buf, line_len);
        if (res < 0) {
            printf("Read error: %s\n", tiny_strerror(res));
            vfs_close(fd);
            return 5;
        }
        else if ((size_t)res > line_len) {
            printf("BUFFER OVERRUN! %d > %lu\n", res, (unsigned long)line_len);
            vfs_close(fd);
            return 6;
        }
        else if (res == 0) {
            /* EOF */
            printf("-- EOF --\n");
            break;
        }
        printf("%08lx:", (unsigned long)offset);
        for (int k = 0; k < res; ++k) {
            if ((k % 2) == 0) {
                putchar(' ');
            }
            printf("%02x", buf[k]);
        }
        for (unsigned k = res; k < sizeof(buf); ++k) {
            if ((k % 2) == 0) {
                putchar(' ');
            }
            putchar(' ');
            putchar(' ');
        }
        putchar(' ');
        putchar(' ');
        for (int k = 0; k < res; ++k) {
            if (isprint(buf[k])) {
                putchar(buf[k]);
            }
            else {
                putchar('.');
            }
        }
        puts("");
        offset += res;
        nbytes -= res;
    }

    vfs_close(fd);
    return 0;
}

#ifdef MODULE_XIPFS
#include <limits.h>
#include "fs/xipfs.h"

static int convert(const char *str, uint32_t *val)
{
    char *endptr;
    long l;

    errno = 0;

    l = strtol(str, &endptr, 10);

    if (l == LONG_MIN && errno != 0) {
        return -1;
    }
    if (l == LONG_MAX && errno != 0) {
        return -1;
    }
    if (endptr == str) {
        return -1;
    }
    if ((long unsigned int)l > UINT32_MAX) {
        return -1;
    }
    if (l < 0) {
        return -1;
    }
    if (*endptr != '\0') {
        return -1;
    }

    *val = (uint32_t)l;

    return 0;
}

static int _mk_handler(int argc, char **argv)
{
    uint32_t size, exec;
    char *path;
    int res;

    if (argc < 4) {
        printf("%s <name> <size> <exec>\n", argv[0]);
        return 1;
    }
    path = argv[1];

    res = vfs_normalize_path(path, path, strlen(path) + 1);
    if (res < 0) {
        printf("Invalid path \"%s\": %s\n", path,
            tiny_strerror(res));
        return 1;
    }
    res = convert(argv[2], &size);
    if (res < 0) {
        printf("Invalid size \"%s\": %s\n", argv[2],
            tiny_strerror(res));
        return 1;
    }
    res = convert(argv[3], &exec);
    if (res < 0) {
        printf("Invalid rights \"%s\": %s\n", argv[3],
            tiny_strerror(res));
        return 1;
    }
    if (exec != 0 && exec != 1) {
        printf("Invalid rights \"%s\": %s\n", argv[3],
            tiny_strerror(res));
        return 1;
    }

    res = xipfs_new_file(path, size, exec);
    if (res < 0) {
        printf("Error creating file \"%s\": %s\n", path,
            tiny_strerror(res));
        return 1;
    }

    return 0;
}

static int _exec_handler(int argc, char **argv)
{
    char *path, *exec_argv[EXEC_ARGC_MAX];
    int res, i;

    if (argc < 2) {
        printf("%s <file> [arg0] [arg1] ... [argn]\n", argv[0]);
        return 1;
    }
    path = argv[1];

    res = vfs_normalize_path(path, path, strlen(path) + 1);
    if (res < 0) {
        printf("Invalid path \"%s\": %s\n", path,
            tiny_strerror(res));
        return 1;
    }
    for (i = 1; i < argc && i < EXEC_ARGC_MAX; i++) {
        exec_argv[i-1] = argv[i];
    }
    exec_argv[i-1] = NULL;

    res = xipfs_execv(path, exec_argv);
    if (res < 0) {
        printf("Error executing file \"%s\": %s\n", path,
            tiny_strerror(res));
        return 1;
    }

    return 0;
}
#endif /* MODULE_XIPFS */

static inline int _dehex(char c)
{
    if ('0' <= c && c <= '9') {
        return c - '0';
    }
    else if ('A' <= c && c <= 'F') {
        return c - 'A' + 10;
    }
    else if ('a' <= c && c <= 'f') {
        return c - 'a' + 10;
    }
    else {
        return 0;
    }
}

typedef enum format_e {
    ASCII,
    HEX,
#ifdef MODULE_XIPFS
    B64,
#endif /* MODULE_XIPFS */
} format_t;

#ifdef MODULE_XIPFS
static inline int isb64char(char c)
{
    return (c >= 'A' && c <= 'Z') |
           (c >= 'a' && c <= 'z') |
           (c >= '0' && c <= '9') |
           (c == '+')             |
           (c == '/');
}

static inline int _deb64(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    else if (c >= 'a' && c <= 'z') {
        return 26 + (c - 'a');
    }
    else if (c >= '0' && c <= '9') {
        return 52 + (c - '0');
    }
    else if (c == '+') {
        return 62;
    }
    else if (c == '/') {
        return 63;
    } else {
        return 0;
    }
}
#endif /* MODULE_XIPFS*/

static int _write_handler(int argc, char **argv)
{
    char *w_buf;
    size_t nbytes = 0;
    size_t nb_str = 0;
    char *path = argv[1];
    format_t format;
    int flag = O_CREAT;
    if (argc < 2) {
        puts("vfs write: missing file name");
        return 1;
    }

    if (argc < 3) {
        puts("vfs write: missing format");
        return 1;
    }
    if (strcmp(argv[2], "ascii") == 0) {
        format = ASCII;
    }
    else if (strcmp(argv[2], "hex") == 0) {
        format = HEX;
    }
#ifdef MODULE_XIPFS
    else if (strcmp(argv[2], "b64") == 0) {
        format = B64;
    }
#endif /* MODULE_XIPFS */
    else {
        printf("vfs write: unknown format: %s\n", argv[2]);
        return 1;
    }

    if (argc < 4) {
        puts("vfs write: missing <a|o> flag");
        return 1;
    }
    if (strcmp(argv[3], "a") == 0) {
        flag |= O_WRONLY | O_APPEND;
    }
    else if (strcmp(argv[3], "o") == 0) {
        flag |= O_WRONLY;
    }
    else {
        printf("vfs write: invalid flag %s\n", argv[3]);
        return 1;
    }

    if (argc < 5) {
        puts("vfs write: missing data");
        return 1;
    }
    w_buf = argv[4];
    nbytes = strlen(w_buf);
    /* in hex string mode, bytes may be separated by spaces */
    /* in ascii mode, there could be spaces */
    /* we need the total number of strings to go through */
    nb_str = argc - 4;
    if (format == HEX) {
        /* sanity check: only hex digit and hex strings length must be even */
        for (size_t i = 0; i < nb_str; i++) {
            char c;
            size_t j = 0;
            do {
                c = argv[argc - nb_str + i][j];
                j++;
                if (c != '\0' && !isxdigit((int)c)) {
                    printf("Non-hex character: %c\n", c);
                    return 6;
                }
            } while (c != '\0');
            j--;
            if (j % 2 != 0) {
                puts("Invalid string length");
                return 6;
            }
        }
    }
#ifdef MODULE_XIPFS
    if (format == B64) {
        for (size_t i = 0; i < nb_str; i++) {
            char c;
            size_t j = 0;
            do {
                c = argv[argc - nb_str + i][j];
                j++;
                if (c != '\0' && !isb64char((int)c)) {
                    if (c != '=') {
                        printf("Non-base 64 character: %c\n", c);
                        return 6;
                    }
                    c = argv[argc - nb_str + i][j];
                    j++;
                    if (c != '\0') {
                        if (c != '=') {
                            puts("Expected a '=' padding character\n");
                            return 6;
                        }
                        c = argv[argc - nb_str + i][j];
                        j++;
                        if (c != '\0') {
                            puts("Expected an end-of-line character");
                            return 6;
                        }
                    }
                }
            } while (c != '\0');
            j--;
            if (j % 4 != 0) {
                puts("Invalid string length");
                return 6;
            }
        }
    }
#endif /* MODULE_XIPFS */

    int res;
    res = vfs_normalize_path(path, path, strlen(path) + 1);
    if (res < 0) {
        printf("Invalid path \"%s\": %s\n", path, tiny_strerror(res));
        return 5;
    }

    int fd = vfs_open(path, flag, 0);
    if (fd < 0) {
        printf("Error opening file \"%s\": %s\n", path, tiny_strerror(fd));
        return 3;
    }

    if (format == ASCII) {
        while (nb_str > 0) {
            res = vfs_write(fd, w_buf, nbytes);
            if (res < 0) {
                printf("Write error: %s\n", tiny_strerror(res));
                vfs_close(fd);
                return 4;
            }
            nb_str--;
            if (nb_str) {
                vfs_write(fd, " ", 1);
                w_buf = argv[argc - nb_str];
            }
        }
    }
    else if (format == HEX) {
        while (nb_str > 0) {
            w_buf = argv[argc - nb_str];
            nbytes = strlen(w_buf);
            while (nbytes > 0) {
                uint8_t byte = _dehex(*w_buf) << 4 | _dehex(*(w_buf + 1));
                res = vfs_write(fd, &byte, 1);
                if (res < 0) {
                    printf("Write error: %s\n", tiny_strerror(res));
                    vfs_close(fd);
                    return 4;
                }
                w_buf += 2;
                nbytes -= 2;
            }
            nb_str--;
        }
    }
#ifdef MODULE_XIPFS
    else if (format == B64) {
        while (nb_str > 0) {
            uint32_t bytes, r;
            char buf[3];
            w_buf = argv[argc - nb_str];
            nbytes = strlen(w_buf);
            while (nbytes > 0) {
                bytes = 0;
                r = 3;
                assert(w_buf[0] != '=');
                bytes |= _deb64(w_buf[0]) << 18;
                assert(w_buf[1] != '=');
                bytes |= _deb64(w_buf[1]) << 12;
                if (w_buf[2] != '=') {
                    bytes |= _deb64(w_buf[2]) << 6;
                } else {
                    r--;
                }
                if (w_buf[3] != '=') {
                    bytes |= _deb64(w_buf[3]);
                } else {
                    r--;
                }
                buf[0] = (bytes >> 16) & 0xff;
                buf[1] = (bytes >>  8) & 0xff;
                buf[2] = (bytes      ) & 0xff;
                res = vfs_write(fd, buf, r);
                if (res < 0) {
                    printf("Write error: %s\n", tiny_strerror(res));
                    vfs_close(fd);
                    return 4;
                }
                w_buf += 4;
                nbytes -= 4;
            }
            nb_str--;
        }
    }
#endif /* MODULE_XIPFS */
    else {
        printf("error\n");
        return -1;
    }

    vfs_close(fd);
    return 0;
}

static int _cp_handler(int argc, char **argv)
{
    if (argc < 3) {
        _vfs_usage(argv);
        return 1;
    }
    char *src_name = argv[1];
    char *dest_name = argv[2];

    int res;
    res = vfs_normalize_path(src_name, src_name, strlen(src_name) + 1);
    if (res < 0) {
        printf("Invalid path \"%s\": %s\n", src_name, tiny_strerror(res));
        return 5;
    }
    res = vfs_normalize_path(dest_name, dest_name, strlen(dest_name) + 1);
    if (res < 0) {
        printf("Invalid path \"%s\": %s\n", dest_name, tiny_strerror(res));
        return 5;
    }

    printf("%s: copy src: %s dest: %s\n", argv[0], src_name, dest_name);

    int fd_in = vfs_open(src_name, O_RDONLY, 0);
    if (fd_in < 0) {
        printf("Error opening file for reading \"%s\": %s\n", src_name,
               tiny_strerror(fd_in));
        return 2;
    }
    int fd_out = vfs_open(dest_name, O_WRONLY | O_TRUNC | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (fd_out < 0) {
        printf("Error opening file for writing \"%s\": %s\n", dest_name,
               tiny_strerror(fd_out));
        return 2;
    }
    int eof = 0;
    while (eof == 0) {
        size_t bufspace = sizeof(_shell_vfs_data_buffer);
        size_t pos = 0;
        while (bufspace > 0) {
            int res = vfs_read(fd_in, &_shell_vfs_data_buffer[pos], bufspace);
            if (res < 0) {
                printf("Error reading %lu bytes @ 0x%lx in \"%s\" (%d): %s\n",
                       (unsigned long)bufspace, (unsigned long)pos, src_name,
                       fd_in, tiny_strerror(res));
                vfs_close(fd_in);
                vfs_close(fd_out);
                return 2;
            }
            if (res == 0) {
                /* EOF */
                eof = 1;
                break;
            }
            if (((unsigned)res) > bufspace) {
                printf("READ BUFFER OVERRUN! %d > %lu\n", res, (unsigned long)bufspace);
                vfs_close(fd_in);
                vfs_close(fd_out);
                return 3;
            }
            pos += res;
            bufspace -= res;
        }
        bufspace = pos;
        pos = 0;
        while (bufspace > 0) {
            int res = vfs_write(fd_out, &_shell_vfs_data_buffer[pos], bufspace);
            if (res <= 0) {
                printf("Error writing %lu bytes @ 0x%lx in \"%s\" (%d): %s\n",
                       (unsigned long)bufspace, (unsigned long)pos, dest_name,
                       fd_out, tiny_strerror(res));
                vfs_close(fd_in);
                vfs_close(fd_out);
                return 4;
            }
            if (((unsigned)res) > bufspace) {
                printf("WRITE BUFFER OVERRUN! %d > %lu\n", res, (unsigned long)bufspace);
                vfs_close(fd_in);
                vfs_close(fd_out);
                return 5;
            }
            bufspace -= res;
        }
    }
    printf("Copied: %s -> %s\n", src_name, dest_name);
    vfs_close(fd_in);
    vfs_close(fd_out);
    return 0;
}

static int _mv_handler(int argc, char **argv)
{
    if (argc < 3) {
        _vfs_usage(argv);
        return 1;
    }
    char *src_name = argv[1];
    char *dest_name = argv[2];

    int res;
    res = vfs_normalize_path(src_name, src_name, strlen(src_name) + 1);
    if (res < 0) {
        printf("Invalid path \"%s\": %s\n", src_name, tiny_strerror(res));
        return 5;
    }
    res = vfs_normalize_path(dest_name, dest_name, strlen(dest_name) + 1);
    if (res < 0) {
        printf("Invalid path \"%s\": %s\n", dest_name, tiny_strerror(res));
        return 5;
    }
    printf("%s: move src: %s dest: %s\n", argv[0], src_name, dest_name);

    res = vfs_rename(src_name, dest_name);
    if (res < 0) {
        printf("mv ERR: %s\n", tiny_strerror(res));
        return 2;
    }
    return 0;
}

static int _rm_handler(int argc, char **argv)
{
    if (argc < 2) {
        _vfs_usage(argv);
        return 1;
    }
    bool recursive = !strcmp(argv[1], "-r");
    if (recursive && (argc < 3 || !IS_USED(MODULE_VFS_UTIL))) {
        _vfs_usage(argv);
        return 1;
    }
    char *rm_name = recursive ? argv[2] : argv[1];

    int res;
    res = vfs_normalize_path(rm_name, rm_name, strlen(rm_name) + 1);
    if (res < 0) {
        printf("Invalid path \"%s\": %s\n", rm_name, tiny_strerror(res));
        return 5;
    }
    printf("%s: unlink: %s\n", argv[0], rm_name);

    if (IS_USED(MODULE_VFS_UTIL) && recursive) {
        char pbuf[SHELL_VFS_PATH_SIZE_MAX];
        res = vfs_unlink_recursive(rm_name, pbuf, sizeof(pbuf));
    }
    else {
        res = vfs_unlink(rm_name);
    }
    if (res < 0) {
        printf("rm ERR: %s\n", tiny_strerror(res));
        return 2;
    }
    return 0;
}

static int _mkdir_handler(int argc, char **argv)
{
    if (argc < 2) {
        _vfs_usage(argv);
        return 1;
    }
    char *dir_name = argv[1];

    int res;
    res = vfs_normalize_path(dir_name, dir_name, strlen(dir_name) + 1);
    if (res < 0) {
        printf("Invalid path \"%s\": %s\n", dir_name, tiny_strerror(res));
        return 5;
    }
    printf("%s: mkdir: %s\n", argv[0], dir_name);

    res = vfs_mkdir(dir_name, 0);
    if (res < 0) {
        printf("mkdir ERR: %s\n", tiny_strerror(res));
        return 2;
    }
    return 0;
}

static int _ls_handler(int argc, char **argv)
{
    if (argc < 2) {
        _ls_usage(argv);
        return 1;
    }
    char *path = argv[1];
    int res;
    int ret = 0;
    size_t path_len = strlen(path);
    res = vfs_normalize_path(path, path, path_len + 1);
    if (res < 0) {
        printf("Invalid path \"%s\": %s\n", path, tiny_strerror(res));
        return 5;
    }
    vfs_DIR dir;
    res = vfs_opendir(&dir, path);
    if (res < 0) {
        printf("vfs_opendir error: %s\n", tiny_strerror(res));
        return 1;
    }
    unsigned int nfiles = 0;

    while (1) {
        char path_name[2 * (VFS_NAME_MAX + 1)];
        vfs_dirent_t entry;
        struct stat stat;

        res = vfs_readdir(&dir, &entry);
        if (res < 0) {
            printf("vfs_readdir error: %s\n", tiny_strerror(res));
            if (res == -EAGAIN) {
                /* try again */
                continue;
            }
            ret = 2;
            break;
        }
        if (res == 0) {
            /* end of stream */
            break;
        }

        snprintf(path_name, sizeof(path_name), "%s%s%s", path,
            (path[path_len-1] == '/') ? "" : "/", entry.d_name);
        vfs_stat(path_name, &stat);
        if (stat.st_mode & S_IFDIR) {
            printf("%s%s\n", entry.d_name, (entry.d_name[
                strlen(entry.d_name)-1] == '/') ? "" : "/");
        } else if (stat.st_mode & S_IFREG) {
            printf("%s\t%lu B\n", entry.d_name, stat.st_size);
            ++nfiles;
        } else {
            printf("%s\n", entry.d_name);
        }
    }
    if (ret == 0) {
        printf("total %u files\n", nfiles);
    }

    res = vfs_closedir(&dir);
    if (res < 0) {
        printf("vfs_closedir error: %s\n", tiny_strerror(res));
        return 2;
    }
    return ret;
}

SHELL_COMMAND(ls, "list files", _ls_handler);

static int _vfs_handler(int argc, char **argv)
{
    if (argc < 2) {
        _vfs_usage(argv);
        return 1;
    }
    if (strcmp(argv[1], "r") == 0) {
        /* pass on to read handler, shifting the arguments by one */
        return _read_handler(argc - 1, &argv[1]);
    }
    else if (strcmp(argv[1], "w") == 0) {
        return _write_handler(argc - 1, &argv[1]);
    }
    else if (strcmp(argv[1], "ls") == 0) {
        return _ls_handler(argc - 1, &argv[1]);
    }
    else if (strcmp(argv[1], "cp") == 0) {
        return _cp_handler(argc - 1, &argv[1]);
    }
    else if (strcmp(argv[1], "mv") == 0) {
        return _mv_handler(argc - 1, &argv[1]);
    }
    else if (strcmp(argv[1], "mkdir") == 0) {
        return _mkdir_handler(argc - 1, &argv[1]);
    }
    else if (strcmp(argv[1], "rm") == 0) {
        return _rm_handler(argc - 1, &argv[1]);
    }
    else if (strcmp(argv[1], "df") == 0) {
        return _df_handler(argc - 1, &argv[1]);
    }
    else if (MOUNTPOINTS_NUMOF > 0 && strcmp(argv[1], "mount") == 0) {
        return _mount_handler(argc - 1, &argv[1]);
    }
    else if (MOUNTPOINTS_NUMOF > 0 && strcmp(argv[1], "umount") == 0) {
        return _umount_handler(argc - 1, &argv[1]);
    }
    else if (MOUNTPOINTS_NUMOF > 0 && strcmp(argv[1], "remount") == 0) {
        return _remount_handler(argc - 1, &argv[1]);
    }
    else if (MOUNTPOINTS_NUMOF > 0 && strcmp(argv[1], "format") == 0) {
        return _format_handler(argc - 1, &argv[1]);
    }
#ifdef MODULE_XIPFS
    else if (strcmp(argv[1], "mk") == 0) {
        return _mk_handler(argc - 1, &argv[1]);
    }
    else if (strcmp(argv[1], "exec") == 0) {
        return _exec_handler(argc - 1, &argv[1]);
    }
#endif /* MODULE_XIPFS */
    else {
        printf("vfs: unsupported sub-command \"%s\"\n", argv[1]);
        return 1;
    }
}

SHELL_COMMAND(vfs, "virtual file system operations", _vfs_handler);

#if MODULE_SHELL_CMD_GENFILE
static char _get_char(unsigned i)
{
    i %= 62; /* a-z, A-Z, 0..9, -> 62 characters */

    if (i < 10) {
        return '0' + i;
    }
    i -= 10;

    if (i <= 'z' - 'a') {
        return 'a' + i;
    }
    i -= 1 + 'z' - 'a';

    return 'A' + i;
}

static void _write_block(int fd, unsigned bs, unsigned i)
{
    char block[bs];
    char *buf = block;

    buf += snprintf(buf, bs, "|%03u|", i);

    memset(buf, _get_char(i), &block[bs] - buf);
    block[bs - 1] = '\n';

    vfs_write(fd, block, bs);
}

static int _vfs_genfile_cmd(int argc, char **argv)
{
    unsigned blocksize = 64;
    unsigned blocks = 32;
    int fd = STDOUT_FILENO;

    const char *cmdname = argv[0];
    while (argc > 1 && argv[1][0] == '-') {
        char *optarg = argc > 2 ? argv[2] : NULL;
        char opt = argv[1][1];

        if (optarg == NULL) {
            printf("missing argument\n");
            opt = '?';
        }

        switch (opt) {
        case '?':
            printf("usage: %s [-o <file>] [-b <block size>] [-n num blocks]\n",
                   cmdname);
            return 0;
        case 'o':
            fd = vfs_open(optarg, O_CREAT | O_TRUNC | O_WRONLY, 0644);
            if (fd < 0) {
                printf("can't create %s\n", optarg);
                return fd;
            }
            break;
        case 'b':
            blocksize = atoi(optarg);
            break;
        case 'n':
            blocks = atoi(optarg);
            break;
        default:
            printf("unknown option '%s'\n", argv[1]);
            return 1;
        }
        argc -= 2;
        argv += 2;
    }

    if (!blocksize || !blocks || argc > 1) {
        printf("invalid argument\n");
        return -EINVAL;
    }

    for (unsigned i = 0; i < blocks; ++i) {
        _write_block(fd, blocksize, i);
    }

    if (fd != STDOUT_FILENO) {
        vfs_close(fd);
        printf("%u bytes written.\n", blocksize * blocks);
    }
    return 0;
}
SHELL_COMMAND(genfile, "generate dummy file", _vfs_genfile_cmd);
#endif

__attribute__((used)) /* only used if md5sum / sha1sum / sha256sum is used */
static inline void _print_digest(const uint8_t *digest, size_t len, const char *file)
{
    for (unsigned i = 0; i < len; ++i) {
        printf("%02x", digest[i]);
    }
    printf("  %s\n", file);
}

#if MODULE_SHELL_CMD_MD5SUM
#include "hashes/md5.h"
static int _vfs_md5sum_cmd(int argc, char **argv)
{
    int res;
    uint8_t digest[MD5_DIGEST_LENGTH];

    if (argc < 2) {
        printf("usage: %s [file] …\n", argv[0]);
        return -1;
    }

    for (int i = 1; i < argc; ++i) {
        const char *file = argv[i];
        res = vfs_file_md5(file, digest,
                           _shell_vfs_data_buffer, sizeof(_shell_vfs_data_buffer));
        if (res < 0) {
            printf("%s: error %d\n", file, res);
        } else {
            _print_digest(digest, sizeof(digest), file);
        }
    }

    return 0;
}

SHELL_COMMAND(md5sum, "Compute and check MD5 message digest", _vfs_md5sum_cmd);
#endif

#if MODULE_SHELL_CMD_SHA1SUM
#include "hashes/sha1.h"
static int _vfs_sha1sum_cmd(int argc, char **argv)
{
    int res;
    uint8_t digest[SHA1_DIGEST_LENGTH];

    if (argc < 2) {
        printf("usage: %s [file] …\n", argv[0]);
        return -1;
    }

    for (int i = 1; i < argc; ++i) {
        const char *file = argv[i];
        res = vfs_file_sha1(file, digest,
                           _shell_vfs_data_buffer, sizeof(_shell_vfs_data_buffer));
        if (res < 0) {
            printf("%s: error %d\n", file, res);
        } else {
            _print_digest(digest, sizeof(digest), file);
        }
    }

    return 0;
}

SHELL_COMMAND(sha1sum, "Compute and check SHA1 message digest", _vfs_sha1sum_cmd);
#endif

#if MODULE_SHELL_CMD_SHA256SUM
#include "hashes/sha256.h"
static int _vfs_sha256sum_cmd(int argc, char **argv)
{
    uint8_t digest[SHA256_DIGEST_LENGTH];

    if (argc < 2) {
        printf("usage: %s [file] …\n", argv[0]);
        return -1;
    }

    for (int i = 1; i < argc; ++i) {
        const char *file = argv[i];
        int res = vfs_file_sha256(file, digest, _shell_vfs_data_buffer,
                                  sizeof(_shell_vfs_data_buffer));
        if (res < 0) {
            printf("%s: error %s\n", file, tiny_strerror(res));
        } else {
            _print_digest(digest, sizeof(digest), file);
        }
    }

    return 0;
}

SHELL_COMMAND(sha256sum, "Compute and check SHA256 message digest", _vfs_sha256sum_cmd);
#endif

#endif
