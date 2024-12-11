ifneq (,$(filter -lc,$(LINKFLAGS)))
  LINKFLAGS := $(patsubst -lc,-L$(BINDIR)/newlib_nano_pip -lc,$(LINKFLAGS))
else
  LINKFLAGS += -L$(BINDIR)/newlib_nano_pip -lc
endif
