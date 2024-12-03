# Copyright (C) 2024 Université de Lille
#
# This file is subject to the terms and conditions of the GNU Lesser
# General Public License v2.1. See the file LICENSE in the top level
# directory for more details.

PREFIX          = arm-none-eabi-
CC              = $(PREFIX)gcc
LD              = $(PREFIX)ld
OBJCOPY         = $(PREFIX)objcopy

CFLAGS          = -Wall
CFLAGS         += -Wextra
CFLAGS         += -Werror
CFLAGS         += -mthumb
CFLAGS         += -mcpu=cortex-m4
CFLAGS         += -ffreestanding
ifdef DEBUG
CFLAGS         += -Og
CFLAGS         += -ggdb
else
CFLAGS         += -Os
endif

LDFLAGS         = -Tlink.ld

OBJCOPYFLAGS    = --input-target=elf32-littlearm
OBJCOPYFLAGS   += --output-target=binary

all: crt0.bin

crt0.bin: crt0.elf
	$(OBJCOPY) $(OBJCOPYFLAGS) $< $@
	@chmod 644 $@

crt0.elf: crt0.o
	$(LD) $(LDFLAGS) $< -o $@

crt0.o: crt0.c
	$(CC) $(CFLAGS) -c $< -o $@

realclean: clean
	$(RM) crt0.elf crt0.bin

clean:
	$(RM) crt0.o

.PHONY: all realclean clean
