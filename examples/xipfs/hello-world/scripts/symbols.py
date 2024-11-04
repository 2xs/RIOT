#!/usr/bin/env python3

# Copyright (C) 2024 Université de Lille
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.


"""Symbols script"""


import sys


from elftools.elf.elffile import ELFFile
from elftools.elf.sections import SymbolTableSection


def usage():
    """Print how to to use the script and exit"""
    print(f'usage: {sys.argv[0]} <ELF> <OUTPUT>')
    sys.exit(1)


def die(message):
    """Print error message and exit"""
    print(f'\033[91;1m{sys.argv[0]}: {message}\033[0m', file=sys.stderr)
    sys.exit(1)


def to_word(x):
    """Convert a python integer to a LE 4-bytes bytearray"""
    return x.to_bytes(4, byteorder='little')


def process_file(elf, symnames):
    """Parse the symbol table sections to extract the st_value"""
    sh = elf.get_section_by_name('.symtab')
    if not sh:
        die(f'.symtab: no section with this name found')
    if not isinstance(sh, SymbolTableSection):
        die(f'.symtab: is not a symbol table section')
    if sh['sh_type'] != 'SHT_SYMTAB':
        die(f'.symtab: is not a SHT_SYMTAB section')
    xs = bytearray()
    for symname in symnames:
        symbols = sh.get_symbol_by_name(symname)
        if not symbols:
            die(f'.symtab: {symname}: no symbol with this name')
        if len(symbols) > 1:
            die(f'.symtab: {symname}: more than one symbol with this name')
        xs += to_word(symbols[0].entry['st_value'])
    return xs


if __name__ == '__main__':
    if len(sys.argv) >= 3:
        with open(sys.argv[1], 'rb') as f:
            xs = process_file(ELFFile(f), [
                # The order of the symbols matter, as it reflects the
                # writing order in the symbols.bin file
                'start',
                '__rom_size',
                '__rom_ram_size',
                '__ram_size',
                '__got_size',
                '__rom_ram_end',
            ])
        with open(sys.argv[2], 'wb') as f:
            f.write(xs)
        sys.exit(0)
    usage()
