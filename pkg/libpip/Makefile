PKG_NAME    = libpip
PKG_URL     = https://github.com/2xs/libpip-riot.git
PKG_VERSION = db4307ae3d30ad6b8aa24c96c00a9e98f91c2fc5
#PKG_VERSION = 0f117f17f920ec2677807154fc532f4180008552
PKG_LICENSE = CeCILL-2.1

include $(RIOTBASE)/pkg/pkg.mk

all:
# Create an empty directory in the BINDIR directory to avoid the
# following error message: "using unknown modules: libpip". This
# is because the directory is not created automatically, since
# there are, for the moment, only header files and no compiled
# object files.
	$(Q)mkdir -p $(BINDIR)/libpip
