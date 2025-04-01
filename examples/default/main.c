/*
 * Copyright (C) 2008, 2009, 2010 Kaspar Schleiser <kaspar@schleiser.de>
 * Copyright (C) 2013 INRIA
 * Copyright (C) 2013 Ludwig Knüpfer <ludwig.knuepfer@fu-berlin.de>
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
 * @brief       Default application that shows a lot of functionality of RIOT
 *
 * @author      Kaspar Schleiser <kaspar@schleiser.de>
 * @author      Oliver Hahm <oliver.hahm@inria.fr>
 * @author      Ludwig Knüpfer <ludwig.knuepfer@fu-berlin.de>
 *
 * @}
 */

#include <stdio.h>
#include <string.h>

#ifdef MODULE_GNRC_XIPFS

#include "shell.h"

#include "cat.h"
#include "cp.h"
#include "exec.h"
#include "fmtbin.h"
#include "hexdump.h"
#include "ldbin.h"
#include "ls.h"
#include "mkbin.h"
#include "put.h"
#include "run.h"
#include "safe_exec.h"

static shell_command_t shell_commands[] = {
    {"cat"      , "print files on the standard output"        , _cat_callback      },
    {"cp"       , "copy files"                                , _cp_callback       },
    {"exec"     , "run a binary in the foreground"            , _exec_callback     },
    {"fmtbin"   , "format the file system"                    , _fmtbin_callback   },
    {"hexdump"  , "ascii and hexadecimal dump"                , _hexdump_callback  },
    {"ldbin"    , "load a chunk of machine code"              , _ldbin_callback    },
    {"ls"       , "list files"                                , _ls_callback       },
    {"mkbin"    , "allocate the space needed to load a binary", _mkbin_callback    },
    {"put"      , "copy a file from the host to the board"    , _put_callback      },
    {"run"      , "run a script from the host to the board"   , _run_callback      },
    {"safe_exec", "run a binary safely in the foreground"     , _safe_exec_callback},
    {NULL, NULL, NULL},
};

#else // MODULE_GNRC_XIPFS

#define shell_commands NULL

#endif // MODULE_GNRC_XIPFS

#ifdef MODULE_NETIF
#include "net/gnrc/pktdump.h"
#include "net/gnrc.h"
#endif

int main(void)
{
#ifdef MODULE_NETIF
    gnrc_netreg_entry_t dump = GNRC_NETREG_ENTRY_INIT_PID(GNRC_NETREG_DEMUX_CTX_ALL,
                                                          gnrc_pktdump_pid);
    gnrc_netreg_register(GNRC_NETTYPE_UNDEF, &dump);
#endif

    (void) puts("Welcome to RIOT!");

    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
