/*
 * Copyright (C) 2024 Université de Lille
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

OUTPUT_FORMAT(
    "elf32-littlearm",
    "elf32-littlearm",
    "elf32-littlearm"
)
OUTPUT_ARCH(arm)
ENTRY(_start)

SECTIONS
{
    .text :
    {
        . = ALIGN( 4 ) ;
        /*
         * The ._start section MUST be the first one
         */
        *(._start)
        *(.text*)
        . = ALIGN( 4 ) ;
        __metadata_off = . ;
    }
}
