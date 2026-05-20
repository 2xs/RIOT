/*
 * Copyright (C) 2024-2025 Université de Lille
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       An application demonstrating xipfs
 *
 * @author      Damien Amara <damien.amara@univ-lille.fr>
 * @author      Gregory Guche <gregory.guche@univ-lille.fr>
 *
 * @}
 */

#include <fcntl.h>
#include <stdlib.h>

#include "fs/xipfs_fs.h"
#include "periph/flashpage.h"
#include "shell.h"
#include "vfs.h"
#include "thread_manager.h"

/**
 * @def PANIC
 *
 * @brief This macro handles fatal errors
 */
#define PANIC() for (;;) {}

/**
 * @def NVME0P0_PAGE_NUM
 *
 * @brief The number of flash page for the nvme0p0 file system
 */
#define NVME0P0_PAGE_NUM 10

/**
 * @def NVME0P1_PAGE_NUM
 *
 * @brief The number of flash page for the nvme0p1 file system
 */
#define NVME0P1_PAGE_NUM 15

/*
 * Allocate a new contiguous space for the nvme0p0 file system
 */
XIPFS_NEW_PARTITION(nvme0p0, "/nvme0p0", NVME0P0_PAGE_NUM);

/*
 * Allocate a new contiguous space for the nvme0p1 file system
 */
XIPFS_NEW_PARTITION(nvme0p1, "/nvme0p1", NVME0P1_PAGE_NUM);

#ifdef BOARD_DWM1001

/**
 * @brief hello-world.fae data blob.
 *
 * To create a *.fae file, you will need to clone the master branch of
 * xipfs_format, that can be found at https://github.com/2xs/XiPFS_Format .
 *
 * Then modify the Makefile to suit your needs/sources and call make.
 * You should end up with a *.fae file ready to be uploaded.
 */
#include "blob/hello-world.fae.h"

#define FILENAME_OF_HELLO_WORLD_FAE  "/nvme0p0/hello-world.fae"
#define SIZEOF_HELLO_WORLD_FAE       (sizeof(hello_world_fae) / sizeof(hello_world_fae[0]))

#include "blob/dumper.fae.h"

#define FILENAME_OF_DUMPER_FAE  "/nvme0p0/dumper.fae"
#define SIZEOF_DUMPER_FAE       (sizeof(dumper_fae) / sizeof(dumper_fae[0]))

#include "blob/normal.fae.h"

#define FILENAME_OF_NORMAL_FAE  "/nvme0p0/normal.fae"
#define SIZEOF_NORMAL_FAE       (sizeof(normal_fae) / sizeof(normal_fae[0]))

typedef struct {
    const char *filename;
    const int   bytesize;
    const unsigned char *data;
    const bool is_executable;
} file_to_drop_t;

static const file_to_drop_t files_to_drop[3] = {
    {FILENAME_OF_HELLO_WORLD_FAE, SIZEOF_HELLO_WORLD_FAE, hello_world_fae, true },
    {FILENAME_OF_DUMPER_FAE, SIZEOF_DUMPER_FAE, dumper_fae, true },
        {FILENAME_OF_NORMAL_FAE, SIZEOF_NORMAL_FAE, normal_fae, true },
};

static int drop_file(const file_to_drop_t *file_to_drop) {
    if (file_to_drop == NULL) {
        return EXIT_FAILURE;
    }

    int file_handle = vfs_open(file_to_drop->filename, O_RDONLY, 0);
    if (file_handle < 0) {

        /* There's no executable file yet, let's drop one */
        int ret = xipfs_extended_driver_new_file(
            file_to_drop->filename, file_to_drop->bytesize, file_to_drop->is_executable
        );
        if (ret < 0) {
            printf("xipfs_extended_driver_new_file : failed to create '%s' : error=%d\n",
                   file_to_drop->filename, ret);
            return EXIT_FAILURE;
        }

        /*
         * Fill it with data
         * Take care : vfs does not support O_APPEND with vfs_write, only O_WRONLY or O_RDWR
         */
        file_handle = vfs_open(file_to_drop->filename, O_WRONLY, 0);
        if (file_handle < 0) {
            printf("vfs_open : failed to open '%s' : error =%d\n",
                   file_to_drop->filename, file_handle);
            return EXIT_FAILURE;
        }

        ssize_t write_ret = vfs_write(file_handle, file_to_drop->data, file_to_drop->bytesize);
        if (write_ret < 0) {
            printf("vfs_write : failed to fill '%s' : error=%d\n",
                   file_to_drop->filename, write_ret);
            vfs_close(file_handle);
            return EXIT_FAILURE;
        }
    }

    vfs_close(file_handle);
    return EXIT_SUCCESS;
}

/**
 * @brief Execution in-place demonstrator.
 *
 * This shell command handler will create a file hello-world.fae on /nvme0p0,
 * if none exists yet from the files_to_drop array.
 */
int drop_files_handler(int argc, char **argv) {
    (void)argc;
    (void)argv;

    for (unsigned int i = 0; i < ARRAY_SIZE(files_to_drop); ++i) {
        if (drop_file(&files_to_drop[i]) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

static task_descriptor_t _tmp_task;

static int cmd_run(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: run <nom du fichier> [args...]");
        return 1;
    }

    _tmp_task.argc = argc;  

    
    strncpy(_tmp_task.argv_buf[0], "execute", ARGV_BUF_SIZE - 1);
    _tmp_task.argv[0] = _tmp_task.argv_buf[0];

    for (int i = 1; i < argc && i < ARGV_MAX; i++) {
        strncpy(_tmp_task.argv_buf[i], argv[i], ARGV_BUF_SIZE - 1);
        _tmp_task.argv_buf[i][ARGV_BUF_SIZE - 1] = '\0';
        _tmp_task.argv[i] = _tmp_task.argv_buf[i];
    }
    _tmp_task.argv[argc] = NULL;

    msg_t msg;
    msg.content.ptr = &_tmp_task;
    msg_send(&msg, thread_manager_get_pid());

    return 0;
}

static shell_command_t shell_commands[] = {
    {"drop_files", "Drop example fae files into /nvme0p0", drop_files_handler},
    {"run", "Run a task", cmd_run},
    {NULL, NULL, NULL},
};

#else /* BOARD_DWM1001 */

static shell_command_t shell_commands[] = { {NULL, NULL, NULL} };

#endif /* BOARD_DWM1001 */

/**
 * @internal
 *
 * @brief Mount a partition, or if it is corrupted, format and
 * remount it
 *
 * @param xipfs_mp A pointer to a memory region containing an
 * xipfs mount point structure
 */
static void mount_or_format(vfs_xipfs_mount_t *xipfs_mp)
{
    if (vfs_mount(&xipfs_mp->vfs_mp) < 0) {
        printf("vfs_mount: \"%s\": file system has not been "
            "initialized or is corrupted\n", xipfs_mp->vfs_mp.mount_point);
        printf("vfs_format: \"%s\": try initializing it\n",
            xipfs_mp->vfs_mp.mount_point);
        vfs_format(&xipfs_mp->vfs_mp);
        printf("vfs_format: \"%s\": OK\n", xipfs_mp->vfs_mp.mount_point);
        if (vfs_mount(&xipfs_mp->vfs_mp) < 0) {
            printf("vfs_mount: \"%s\": file system is corrupted!\n",
                xipfs_mp->vfs_mp.mount_point);
            PANIC();
        }
    }
    printf("vfs_mount: \"%s\": OK\n", xipfs_mp->vfs_mp.mount_point);
}



int main(void)
{

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    printf("Execution de thread_manager_init()\n");
    thread_manager_init();
    mount_or_format(&nvme0p0);
    mount_or_format(&nvme0p1);

    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
