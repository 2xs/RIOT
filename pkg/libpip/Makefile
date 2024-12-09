PKG_NAME    = libpip
PKG_URL     = https://github.com/2xs/libpip-riot.git
PKG_VERSION = fc4ca208f45ebe7515b83273ea6254d56d593dd5
PKG_LICENSE = CeCILL-2.1

include $(RIOTBASE)/pkg/pkg.mk

all:
# Create an empty directory in the BINDIR directory to avoid the
# following error message: "using unknown modules: libpip". This
# is because the directory is not created automatically, since
# there are, for the moment, only header files and no compiled
# object files.
	$(Q)mkdir -p $(BINDIR)/libpip
