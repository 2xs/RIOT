/* ----------------------------------------------------------------------------
 *         SAM Software Package License
 * ----------------------------------------------------------------------------
 * Copyright (c) 2012, Atmel Corporation
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following condition is met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Atmel's name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ----------------------------------------------------------------------------
 */

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
SEARCH_DIR(.)

/* This is only used by gdb to understand where to start */
ENTRY(start)

/* Padding required at the end of each section */
PADDING = 8;

/* XXX */
_eram = 0;
_sram = 0;

/* Section Definitions */
SECTIONS
{
    /*
     * The '.rom' output section collects all
     * input sections that need to remain in ROM.
     */
    .rom :
    {
        . = ALIGN(4);
        __romStart = .;

        _sfixed = .;
        *(.text .text.* .gnu.linkonce.t.*)
        *(.glue_7t) *(.glue_7)
        *(.rodata .rodata* .gnu.linkonce.r.*)
        *(.ARM.extab* .gnu.linkonce.armextab.*)

        /* Support C constructors, and C destructors in both user code
           and the C library. This also provides support for C++ code. */
        . = ALIGN(4);
        KEEP(*(.init))
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP (*(.preinit_array))
        __preinit_array_end = .;

        . = ALIGN(4);
        __init_array_start = .;
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        __init_array_end = .;

        . = ALIGN(0x4);
        KEEP (*crtbegin.o(.ctors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*crtend.o(.ctors))

        . = ALIGN(4);
        KEEP(*(.fini))

        . = ALIGN(4);
        __fini_array_start = .;
        KEEP (*(.fini_array))
        KEEP (*(SORT(.fini_array.*)))
        __fini_array_end = .;

        KEEP (*crtbegin.o(.dtors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .dtors))
        KEEP (*(SORT(.dtors.*)))
        KEEP (*crtend.o(.dtors))

        . = ALIGN(4);
        _efixed = .;            /* End of text section */

        /*
         * TLS relocations are offsets relative to the address
         * of the first TLS symbol. That means we just need to
         * allocate them all together so that the TLS region
         * is compact when allocated for each thread.
         */

        /*
         * TLS initialization data is loaded into ROM so that
         * each thread can get its values initialized from there
         * at startup
         */
         __tdata_start = .;
        *(.tdata .tdata.* .gnu.linkonce.td.*)
        __tdata_end = .;

         __tdata_source = __tdata_start;
        __tdata_size = __tdata_end - __tdata_start;

        /*
         * TLS zeroed data is relocated as if it immediately followed
         * the tdata values. However, the linker 'magically' erases the
         * memory allocation so that no ROM is consumed by this
         * section
         */
        *(.tbss .tbss.* .gnu.linkonce.tb.*)
        *(.tcommon)
        __tbss_end = .;

        __tls_size = __tbss_end - __tdata_start;
        __tbss_size = __tls_size - __tdata_size;
        PROVIDE( __tbss_offset = ADDR(.tbss) - ADDR(.tdata) );
        PROVIDE( __tls_align = MAX(ALIGNOF(.tdata), ALIGNOF(.tbss)) );
        PROVIDE( __arm32_tls_tcb_offset = MAX(8, __tls_align) );
        PROVIDE( __arm64_tls_tcb_offset = MAX(16, __tls_align) );

        /* .ARM.exidx is sorted, so has to go in its own output section.  */
        PROVIDE_HIDDEN (__exidx_start = .);
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        PROVIDE_HIDDEN (__exidx_end = .);

        /* exception handling */
        . = ALIGN(4);
        KEEP (*(.eh_frame))

        . = ALIGN(4);
        _etext = .;

        KEEP(*(SORT(.flash_writable.*)))

        . = ALIGN(4);
        _end_fw = .;

        . = ALIGN(4);
        . += PADDING;
        __romEnd = .;
    }

    __romSize = __romEnd - __romStart;

    /*
     * The section '.got' must be placed rigth
     * after the '.rom' section in order to save
     * ROM space.
     */
    .got :
    {
        . = ALIGN(4);
        __gotStart = .;

        *(.got*)
        . = ALIGN(4);

        . = ALIGN(4);
        . += PADDING;
        __gotEnd = .;
    }

    __gotSize = __gotEnd - __gotStart;

    /*
     * The '.rom.ram' output section collects all
     * input sections that need to be copied from
     * ROM to RAM.
     */
    .rom.ram :
    {
        . = ALIGN(4);
        __romRamStart = .;

        KEEP (*(SORT(.roxfa.*)))

        _srelocate = .;
        *(.ramfunc .ramfunc.*);
        *(.data .data.*);
        KEEP (*(SORT(.xfa.*)))
        KEEP (*(.openocd .openocd.*))
        . = ALIGN(4);
        _erelocate = .;

        _sbackup_data = .;
        *(.backup.data)
        _ebackup_data = .;
        /* Round size so that we can use 4 byte copy in init */
        . = ALIGN(4);

        . = ALIGN(4);
        . += PADDING;
        __romRamEnd = .;
    }

    __romRamSize = __romRamEnd - __romRamStart;

    /*
     * The '.ram' output section collects all
     * input sections that need to be set to zero
     * in RAM.
     */
    .ram :
    {
        . = ALIGN(4);
        __ramStart = .;

        /* .bss section which is used for uninitialized data */
        _sbss = .;
        _szero = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        _ezero = .;

        /*
         * collect all uninitialized sections that go into RAM
         */
        __noinit_start = .;
        *(.noinit)
        . = ALIGN(4);
        __noinit_end = .;

        _sbackup_bss = .;
        *(.backup.bss)
        _ebackup_bss = .;
        /* Round size so that we can use 4 byte copy in init */
        . = ALIGN(4);

        _sheap1 = .;
        _eheap1 = _sheap1;

        . = ALIGN(4);
        . += PADDING;
        __ramEnd = .;
    }

    __ramSize = __ramEnd - __ramStart;
}
