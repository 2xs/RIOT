#!/usr/bin/env python3

# Copyright (C) 2024 Université de Lille
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.


"""gdbinit script"""


import sys


from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


def usage():
    """Print how to to use the script and exit"""
    print(f'usage: {sys.argv[0]} <CRT0_PATH> <SOFTWARE_PATH> <METADATA_SIZE>')
    sys.exit(1)


def die(message):
    """Print error message and exit"""
    print(f'\033[91;1m{sys.argv[0]}: {message}\033[0m', file=sys.stderr)
    sys.exit(1)


def process_file(elf, symnames):
    """Parse the symbol table sections to extract the st_value"""
    sh = elf.get_section_by_name('.symtab')
    if not sh:
        die(f'.symtab: no section with this name found')
    if not isinstance(sh, SymbolTableSection):
        die(f'.symtab: is not a symbol table section')
    if sh['sh_type'] != 'SHT_SYMTAB':
        die(f'.symtab: is not a SHT_SYMTAB section')
    xs = []
    for symname in symnames:
        symbols = sh.get_symbol_by_name(symname)
        if not symbols:
            die(f'.symtab: {symname}: no symbol with this name')
        if len(symbols) > 1:
            die(f'.symtab: {symname}: more than one symbol with this name')
        xs.append(symbols[0].entry['st_value'])
    return xs


if __name__ == '__main__':
    if len(sys.argv) >= 4:
        crt0_path = sys.argv[1]
        soft_path = sys.argv[2]
        crt0_meta_size = int(sys.argv[3])
        with open(soft_path, 'rb') as f:
            xs = process_file(ELFFile(f), [
                '__rom_size',
                '__got_size',
                '__rom_ram_size',
                '__ram_size',
            ])
        text_size = xs[0]
        got_size = xs[1]
        data_size = xs[2]
        bss_size = xs[3]
        print(f'set $flash_base = # Define the flash base address here')
        print(f'set $ram_base = # Define the RAM base address here')
        print(f'set $crt0_text = $flash_base')
        print(f'set $text = $crt0_text + {crt0_meta_size}')
        print(f'set $got = $text + {text_size}')
        print(f'set $data = $got + {got_size}')
        print(f'set $rel_got = $ram_base')
        print(f'set $rel_data = $rel_got + {got_size}')
        print(f'set $bss = $rel_data + {data_size}')
        print(f'add-symbol-file {crt0_path} '
               '-s .text $crt0_text')
        print(f'add-symbol-file {soft_path} '
               '-s .rom $text '
               '-s .got $rel_got '
               '-s .rom.ram $rel_data '
               '-s .ram $bss')
        print('set $flash_end = $flash_base + '
              f'{crt0_meta_size + text_size + got_size + data_size}')
        print('set $ram_end = $ram_base + '
              f'{got_size + data_size + bss_size}')
        sys.exit(0)
    usage()
