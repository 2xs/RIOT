/*
 * Copyright (C) 2024 Université de Lille
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
 * @brief       CRT0 implementation
 *
 * @author      Damien Amara <damien.amara@univ-lille.fr>
 *
 * @}
 */

#include <stdint.h>
#include <stddef.h>

/*
 * WARNING: No global variable must be declared in this file!
 */

#ifdef __GNUC__

/**
 * @internal
 *
 * @def NORETURN
 *
 * @brief Instructs the compiler that the function will not
 * return
 *
 * @warning The _start function must have this attribute
 */
#define NORETURN __attribute__((noreturn))

/**
 * @internal
 *
 * @def NAKED
 *
 * @brief Instructs the compiler that the function will not have
 * prologue/epilogue sequences
 */
#define NAKED __attribute__((naked))

/**
 * @internal
 *
 * @def UNUSED
 *
 * @brief Instructs the compiler that the function/variable is meant
 * to be possibly unused
 */
#define UNUSED __attribute__((unused))

/**
 * @internal
 *
 * @def SECTION
 *
 * @brief Instructs the compiler that a variable (or function)
 * lives in a particular section
 *
 * @param name The section name
 */
#define SECTION(name) __attribute__((section(name)))

#else

#error "GCC is required to compile this source file"

#endif /* __GNUC__ */

/**
 * @internal
 *
 * @def LDM_STM_NB_BYTES_COPIED
 *
 * @brief Number of bytes copied by the LDM and STM instructions
 */
#define LDM_STM_NB_BYTES_COPIED (48)

/**
 * @internal
 *
 * @def LDRD_STRD_NB_BYTES_COPIED
 *
 * @brief Number of bytes copied by the LDRD and STRD
 * instructions
 */
#define LDRD_STRD_NB_BYTES_COPIED (8)

/**
 * @internal
 *
 * @def LDR_STR_NB_BYTES_COPIED
 *
 * @brief Number of bytes copied by the LDR and STR instructions
 */
#define LDR_STR_NB_BYTES_COPIED (4)

/**
 * @internal
 *
 * @def LDRB_STRB_NB_BYTES_COPIED
 *
 * @brief Number of bytes copied by the LDRB and STRB
 * instructions
 */
#define LDRB_STRB_NB_BYTES_COPIED (1)

/**
 * @internal
 *
 * @def ROUND
 *
 * @brief Round x to the next power of two y
 *
 * @param x The number to round to the next power of two y
 *
 * @param y The power of two with which to round x
 */
#define ROUND(x, y) (((x) + (y) - 1) & ~((y) - 1))

/**
 * @internal
 *
 * @define THUMB_ADDRESS
 *
 * @brief Calculate the odd address for Thumb mode from x
 *
 * @param x The address to convert to thumb mode
 */
#define THUMB_ADDRESS(x) ((x) | 1)

/**
 * @internal
 *
 * @def ERR_MSG_PREFIX
 *
 * @brief Prefix of error messages
 */
#define ERR_MSG_PREFIX "crt0: "

/**
 * @internal
 *
 * @def ERR_MSG_1
 *
 * @brief Error message number 1
 */
#define ERR_MSG_1 "not enough ram"

/**
 * @internal
 *
 * @def ERR_MSG_2
 *
 * @brief Error message number 2
 */
#define ERR_MSG_2 "out-of-bounds offset"

/**
 * @internal
 *
 * @def ERR_MSG_3
 *
 * @brief Error message number 3
 */
#define ERR_MSG_3 "cannot relocate offsets in .rom"

/**
 * @internal
 *
 * @def ERR_MSG_4
 *
 * @brief Error message number 4
 */
#define ERR_MSG_4 "cannot relocate offsets in .got"

/**
 * @internal
 *
 * @def SYS_WRITE0
 *
 * @brief Semihosting software interrupts (SWIs) to write a
 * null-terminated string to the console.
 */
#define SYS_WRITE0 "4"

/**
 * @internal
 *
 * @def ANGEL_SWI
 *
 * @brief Value indicating to the host that we are requesting a
 * semihosting operation.
 */
#define ANGEL_SWI  "0xab"

/**
 * @def PANIC
 *
 * @brief This macro handles fatal errors
 */
#define PANIC() for (;;);

/**
 * @internal
 *
 * @brief Enumeration of error message identifiers
 */
typedef enum err_msg_id_u {
    /**
     * Identifier of error message 1
     */
    ERR_MSG_ID_1,
    /**
     * Identifier of error message 2
     */
    ERR_MSG_ID_2,
    /**
     * Identifier of error message 3
     */
    ERR_MSG_ID_3,
    /**
     * Identifier of error message 4
     */
    ERR_MSG_ID_4,
} err_msg_id_t;

/**
 * @internal
 *
 * @brief Data structure that describes a symbol table
 */
typedef struct symbol_table_s
{
    /**
     * The entry point address within the partition
     */
    uint32_t entry_point;
    /**
     * The .rom section size, in bytes, of the partition
     */
    uint32_t rom_sec_size;
    /**
     * The .rom.ram section size, in bytes, of the partition
     */
    uint32_t rom_ram_sec_size;
    /**
     * The .ram section size, in bytes, of the partition
     */
    uint32_t ram_sec_size;
    /**
     * The .got section size, in bytes, of the partition
     */
    uint32_t got_sec_size;
    /**
     * The .rom.ram section end address of the partition
     */
    uint32_t rom_ram_end;
} symbol_table_t;

/**
 * @internal
 *
 * @brief Data structure that describes a patch info entry
 */
typedef struct patchinfo_entry_s
{
    /**
     * The pointer offest to patch
     */
    uint32_t ptr_off;
} patchinfo_entry_t;

/**
 * @internal
 *
 * @brief Data structure that describes a path info table
 */
typedef struct patchinfo_table_s
{
    /**
     * The number of patchinfo entry
     */
    uint32_t entry_number;
    /**
     * The patchinfo entries
     */
    patchinfo_entry_t entries[];
} patchinfo_table_t;

/**
 * @internal
 *
 * @brief Data structure that describes metadata
 */
typedef struct metadata_s
{
    /**
     * The symbol table
     */
    symbol_table_t symbol_table;
    /**
     * The patchinfo table
     */
    patchinfo_table_t patchinfo_table;
} metadata_t;

/**
 * @internal
 *
 * @brief Data structure that describes the memory layout
 * required by the CRT0 to execute the relocatable binary
 */
typedef struct crt0_ctx_s {
    /**
     * Start address of the binary in the NVM
     */
    void *bin_base;
    /**
     * Start address of the available free RAM
     */
    void *ram_start;
    /**
     * End address of the available free RAM
     */
    void *ram_end;
    /**
     * Start address of the free NVM
     */
    void *nvm_start;
    /**
     * End address of the free NVM
     */
    void *nvm_end;
} crt0_ctx_t;

/**
 * @internal
 *
 * @brief the offset of the metadata structure
 */
extern uint32_t *__metadata_off;

/*
 * Fonction prototypes
 */

static inline void* memcpy(void *dest, const void *src, size_t n);
static NAKED void die(err_msg_id_t id UNUSED);

/**
 * @pre ctx is a pointer to a memory region containg an
 * accessible and valid CRT0 data structure
 *
 * @warning This function must be positioned first in the file
 * to ensure that the first instruction generated for it is
 * located at offset 0 in the output binary file
 *
 * @see -fno-toplevel-reorder GCC option in the Makefile
 *
 * @brief The entry point of CRT0 is responsible for copying
 * data from NVM to RAM, initializing RAM to zero, and applying
 * patch information to enable post-issuance software deployment
 *
 * @param ctx A pointer to a memory region containg a CRT0 data
 * structure
 */
SECTION("._start") NORETURN void _start(crt0_ctx_t *ctx)
{
    /* get memory layout */
    uint32_t binary_addr = (uint32_t)ctx->bin_base;
    uint32_t unused_ram_addr = (uint32_t)ctx->ram_start;
    uint32_t ram_end_addr = (uint32_t)ctx->ram_end;

    /* get metadata */
    metadata_t *metadata = (metadata_t *)
        (binary_addr + (uint32_t) &__metadata_off);
    uint32_t entry_point_offset = metadata->symbol_table.entry_point;
    uint32_t rom_sec_size = metadata->symbol_table.rom_sec_size;
    uint32_t got_sec_size = metadata->symbol_table.got_sec_size;
    uint32_t rom_ram_sec_size = metadata->symbol_table.rom_ram_sec_size;
    uint32_t ram_sec_size = metadata->symbol_table.ram_sec_size;

    /* calculate section start address in ROM */
    uint32_t rom_sec_addr =
        (uint32_t) metadata + sizeof(metadata->symbol_table) +
        sizeof(metadata->patchinfo_table.entry_number) +
        metadata->patchinfo_table.entry_number *
        sizeof(patchinfo_entry_t);
    uint32_t got_sec_addr = rom_sec_addr + rom_sec_size;
    uint32_t rom_ram_sec_addr = got_sec_addr + got_sec_size;
    uint32_t entry_point_addr = THUMB_ADDRESS(rom_sec_addr + entry_point_offset);

    /* calculate relocated section start address in RAM */
    uint32_t rel_got_sec_addr = unused_ram_addr;
    uint32_t rel_rom_ram_sec_addr = rel_got_sec_addr + got_sec_size;
    uint32_t rel_ram_sec_addr = rel_rom_ram_sec_addr + rom_ram_sec_size;

    /* check if sufficient RAM is available for relocation */
    if (rel_got_sec_addr + got_sec_size > ram_end_addr ||
        rel_rom_ram_sec_addr + rom_ram_sec_size > ram_end_addr ||
        rel_ram_sec_addr + ram_sec_size > ram_end_addr) {
        die(ERR_MSG_ID_1);
    }

    /* update unused RAM value */
    ctx->ram_start = (void *)(rel_ram_sec_addr + ram_sec_size);
    /* Update unused ROM value */
    ctx->nvm_start = (void *)ROUND(
        (uintptr_t)ctx->nvm_start +
        ((uint32_t)&__metadata_off) +
        sizeof(metadata_t) +
        metadata->symbol_table.rom_ram_end, 32);

    /* relocate .rom.ram section */
    (void)memcpy((void *) rel_rom_ram_sec_addr,
                 (void *) rom_ram_sec_addr,
                 (size_t) rom_ram_sec_size);

    /* initialize .ram section */
    for (size_t i = 0; (i << 2) < ram_sec_size; i++) {
        ((uint32_t *) rel_ram_sec_addr)[i] = 0;
    }

    /* 
     * Relocate the '.got' section from ROM to RAM,
     * dynamically updating each global variable offset
     * - originally relative to the binary file's start - to
     * the new memory addresses where they are now located
     */
    for (size_t i = 0; (i << 2) < got_sec_size; i++) {
        uint32_t off = ((uint32_t *) got_sec_addr)[i];
        uint32_t addr = 0;
        if (off < rom_sec_size) {
            addr = rom_sec_addr + off;
            goto valid_got_entry;
        }
        off -= rom_sec_size;
        if (off < got_sec_size) {
            /* offset must always be zero for the
             * _rom_size symbol */
            addr = rel_got_sec_addr + off;
            goto valid_got_entry;
        }
        off -= got_sec_size;
        if (off < rom_ram_sec_size) {
            addr = rel_rom_ram_sec_addr + off;
            goto valid_got_entry;
        }
        off -= rom_ram_sec_size;
        if (off < ram_sec_size) {
            addr = rel_ram_sec_addr + off;
            goto valid_got_entry;
        }
        die(ERR_MSG_ID_2);
valid_got_entry:
        ((uint32_t *) rel_got_sec_addr)[i] = addr;
    }

    /*
     * Update each global pointer by assigning the relocated
     * address of the value it points to the corresponding
     * relocated pointer address
     */
    for (size_t i = 0; i < metadata->patchinfo_table.entry_number; i++) {
        uint32_t ptr_off = metadata->patchinfo_table.entries[i].ptr_off;
        uint32_t off = *((uint32_t *)(rom_sec_addr + ptr_off));
        uint32_t ptr_addr = 0, addr = 0;
        if (ptr_off < rom_sec_size) {
            goto ptr_off_in_rom;
        }
        ptr_off -= rom_sec_size;
        if (ptr_off < got_sec_size) {
            goto off_in_got;
        }
        ptr_off -= got_sec_size;
        if (ptr_off < rom_ram_sec_size) {
            ptr_addr = rel_rom_ram_sec_addr + ptr_off;
            goto valid_ptr_addr;
        }
        ptr_off -= rom_ram_sec_size;
        if (ptr_off < ram_sec_size) {
            ptr_addr = rel_ram_sec_addr + ptr_off;
            goto valid_ptr_addr;
        }
        goto off_out_bounds;
valid_ptr_addr:
        if (off < rom_sec_size) {
            addr = rom_sec_addr + off;
            goto valid_addr;
        }
        off -= rom_sec_size;
        if (off < got_sec_size) {
            goto off_in_got;
        }
        off -= got_sec_size;
        if (off < rom_ram_sec_size) {
            addr = rel_rom_ram_sec_addr + off;
            goto valid_addr;
        }
        off -= rom_ram_sec_size;
        if (off < ram_sec_size) {
            addr = rel_ram_sec_addr + off;
            goto valid_addr;
        }
off_out_bounds:
        die(ERR_MSG_ID_2);
ptr_off_in_rom:
        die(ERR_MSG_ID_3);
off_in_got:
        die(ERR_MSG_ID_4);
valid_addr:
        *((uint32_t *) ptr_addr) = addr;
    }

    /*
     * Set R0 to the address of the first parameter passed to
     * the start() function, initialize R10 with the address of
     * the relocated GOT, and branch to the address of the
     * start() function
     */
    __asm__ volatile
    (
        "   mov    r0, %0 \n"
        "   mov    sl, %1 \n"
        "   bx     %2     \n"
        :
        : "r" (ctx),
          "r" (rel_got_sec_addr),
          "r" (entry_point_addr)
        : "r0", "r1", "sl"
    );

    PANIC();
}

/**
 * @brief A version of memcpy optimized for Cortex-M4
 *
 * @param dest Destination memory area
 *
 * @param src Source memory area
 *
 * @param n Number of bytes to copy
 *
 * @return Returns a pointer to dest
 *
 * @see Cortex-M4 Technical Reference Manual 3.3.1 Cortex-M4
 * instructions
 */
static inline void* memcpy(void *dest,
                           const void *src,
                           size_t n)
{
    const char *src0 = src;
    char *dest0 = dest;

    while (n >= LDM_STM_NB_BYTES_COPIED) {
        __asm__ volatile
        (
            "ldmia %0!, {r2-r12,r14}\n"
            "stmia %1!, {r2-r12,r14}\n"
            : "+r" (src0), "+r" (dest0)
            :
            :  "r2",  "r3",  "r4",  "r5",
               "r6",  "r7",  "r8",  "r9",
              "r10", "r11", "r12", "r14",
              "memory"
        );
        n -= LDM_STM_NB_BYTES_COPIED;
    }
    while (n >= LDRD_STRD_NB_BYTES_COPIED) {
        __asm__ volatile
        (
            "ldrd r2, r3, [%0], #8\n"
            "strd r2, r3, [%1], #8\n"
            : "+r" (src0), "+r" (dest0)
            :
            : "r2", "r3", "memory"
        );
        n -= LDRD_STRD_NB_BYTES_COPIED;
    }
    if (n >= LDR_STR_NB_BYTES_COPIED) {
        __asm__ volatile
        (
            "ldr r2, [%0], #4\n"
            "str r2, [%1], #4\n"
            : "+r" (src0), "+r" (dest0)
            :
            : "r2", "memory"
        );
        n -= LDR_STR_NB_BYTES_COPIED;
    }
    while (n >= LDRB_STRB_NB_BYTES_COPIED) {
        __asm__ volatile
        (
            "ldrb r2, [%0], #1\n"
            "strb r2, [%1], #1\n"
            : "+r" (src0), "+r" (dest0)
            :
            : "r2", "memory"
        );
        n -= LDRB_STRB_NB_BYTES_COPIED;
    }

    return dest;
}

/**
 * @brief Print error message and stop execution
 *
 * @param Identifier of the message to print
 */
static NAKED void die(err_msg_id_t id UNUSED)
{
    __asm__ volatile
    (
        "   mov    r2, r0                 \n"
        "   mov    r0, #" SYS_WRITE0 "    \n"
        "   adr.w  r1, 3f                 \n"
        "   bkpt   " ANGEL_SWI "          \n"
        "   mov    r0, #" SYS_WRITE0 "    \n"
        "   adr.w  r3, 1f                 \n"
        "   add.w  r2, r3, r2, lsl #3     \n"
        "   orr.w  r2, #1                 \n"
        "   bx     r2                     \n"
        "1: adr.w  r1, 4f                 \n"
        "   b.w    2f                     \n"
        "   adr.w  r1, 5f                 \n"
        "   b.w    2f                     \n"
        "   adr.w  r1, 6f                 \n"
        "   b.w    2f                     \n"
        "   adr.w  r1, 7f                 \n"
        "2: bkpt   " ANGEL_SWI "          \n"
        "   b      .                      \n"
        "3: .asciz \"" ERR_MSG_PREFIX "\" \n"
        "4: .asciz \"" ERR_MSG_1 "\\n\"   \n"
        "5: .asciz \"" ERR_MSG_2 "\\n\"   \n"
        "6: .asciz \"" ERR_MSG_3 "\\n\"   \n"
        "7: .asciz \"" ERR_MSG_4 "\\n\"   \n"
        "   .align 1                      \n"
    );
}
