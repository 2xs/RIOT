PKG_NAME    = newlib_nano_pip
PKG_URL     = https://sourceware.org/git/newlib-cygwin.git
PKG_VERSION = 415fdd4279b85eeec9d54775ce13c5c412451e08
PKG_LICENSE = MIT/BSD

include $(RIOTBASE)/pkg/pkg.mk

# Quiet both stdout and stderr if the QUIET flag is set.
ifeq ($(QUIET),1)
  QO = 2>/dev/null 1>/dev/null
else
  QO =
endif

# Retrieve the directory name of the multilib to compile from
# the compilation flags.
CONFIGUREDIR       = $(PKG_SOURCE_DIR)/build
TARGETDIR          = $(shell $(CC) -dumpmachine)
MULTILIBDIR        = $(shell $(CC) $(CFLAGS) -print-multi-directory)
LIBRARYSRCDIR      = $(CONFIGUREDIR)/$(TARGETDIR)/$(MULTILIBDIR)/newlib
LIBRARYDSTDIR      = $(BINDIR)/$(PKG_NAME)

# These compilation flags are necessary in order to compile the
# newlib in a relocatable manner.
CFLAGS_FOR_TARGET  = -msingle-pic-base
CFLAGS_FOR_TARGET += -mpic-register=r10
CFLAGS_FOR_TARGET += -mno-pic-data-is-text-relative
CFLAGS_FOR_TARGET += -fPIC
CFLAGS_FOR_TARGET += -Os

# These configuration flags allow to compile the nano version of
# the newlib. They have been retrieved from the
# build-toolchain.sh script in the Arm GNU Toolchain sources.
CONFIGUREFLAGS     = --target=$(TARGETDIR)
CONFIGUREFLAGS    += --disable-newlib-supplied-syscalls
CONFIGUREFLAGS    += --enable-newlib-reent-check-verify
CONFIGUREFLAGS    += --enable-newlib-reent-small
CONFIGUREFLAGS    += --enable-newlib-retargetable-locking
CONFIGUREFLAGS    += --disable-newlib-fvwrite-in-streamio
CONFIGUREFLAGS    += --disable-newlib-fseek-optimization
CONFIGUREFLAGS    += --disable-newlib-wide-orient
CONFIGUREFLAGS    += --enable-newlib-nano-malloc
CONFIGUREFLAGS    += --disable-newlib-unbuf-stream-opt
CONFIGUREFLAGS    += --enable-lite-exit
CONFIGUREFLAGS    += --enable-newlib-global-atexit
CONFIGUREFLAGS    += --enable-newlib-nano-formatted-io
CONFIGUREFLAGS    += --disable-nls

.PHONY: all

all: $(LIBRARYDSTDIR)/libc.a $(LIBRARYDSTDIR)/libm.a

$(LIBRARYDSTDIR)/libc.a: $(LIBRARYSRCDIR)/libc.a | $(LIBRARYDSTDIR)
	$(Q) $(QO) cp $< $@

$(LIBRARYDSTDIR)/libm.a: $(LIBRARYSRCDIR)/libm.a | $(LIBRARYDSTDIR)
	$(Q) $(QO) cp $< $@

# TODO: Grouped targets are only supported by GNU Make 4.3 and above...
$(LIBRARYSRCDIR)/libc.a $(LIBRARYSRCDIR)/libm.a &: $(CONFIGUREDIR)/Makefile | $(CONFIGUREDIR)
	$(Q) $(QO) env -i PATH='$(PATH)' MULTILIBDIR='$(MULTILIBDIR)' $(MAKE) -C $(CONFIGUREDIR) -j $(nproc)

$(CONFIGUREDIR)/Makefile: | $(CONFIGUREDIR)
	$(Q) $(QO) cd $(CONFIGUREDIR) && $(QO) env -i PATH='$(PATH)' CFLAGS_FOR_TARGET='$(CFLAGS_FOR_TARGET)' $(PKG_SOURCE_DIR)/configure $(CONFIGUREFLAGS)

$(LIBRARYDSTDIR):
	$(Q) $(QO) mkdir $@

$(CONFIGUREDIR):
	$(Q) $(QO) mkdir $@
