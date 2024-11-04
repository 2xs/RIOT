#!/usr/bin/env python3

# Copyright (C) 2024 Université de Lille
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.


"""Relocation script"""


import sys


from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from elftools.elf.enums import ENUM_RELOC_TYPE_ARM as r_types


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


def get_r_type(r_info):
    """Get the relocation type from r_info"""
    return r_info & 0xff


def process_section(elf, name):
    """Parse a relocation section to extract the r_offset"""
    sh = elf.get_section_by_name(name)
    if not sh:
        return to_word(0)
    if not isinstance(sh, RelocationSection):
        die(f'{name}: is not a relocation section')
    if sh.is_RELA():
        die(f'{name}: unsupported RELA')
    xs = bytearray(to_word(sh.num_relocations()))
    for i, entry in enumerate(sh.iter_relocations()):
        if get_r_type(entry['r_info']) != r_types['R_ARM_ABS32']:
            die(f'{name}: entry {i}: unsupported relocation type')
        xs += to_word(entry['r_offset'])
    return xs


def process_file(elf, names):
    """Process each section"""
    xs = bytearray()
    for name in names:
        xs += process_section(elf, name)
    return xs


if __name__ == '__main__':
    if len(sys.argv) >= 3:
        with open(sys.argv[1], 'rb') as f:
            xs = process_file(ELFFile(f), [
                '.rel.rom.ram',
            ])
        with open(sys.argv[2], 'wb') as f:
            f.write(xs)
        sys.exit(0)
    usage()
