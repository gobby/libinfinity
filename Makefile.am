SUBDIRS = libinfinity libinftext

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
	libinfinity-$(LIBINFINITY_API_VERSION).pc \
	libinftext-$(LIBINFINITY_API_VERSION).pc

if WITH_INFGTK
SUBDIRS += libinfgtk
pkgconfig_DATA += libinf$(infgtkver)-$(LIBINFINITY_API_VERSION).pc
endif

if WITH_INFTEXTGTK
SUBDIRS += libinftextgtk
pkgconfig_DATA += libinftext$(infgtkver)-$(LIBINFINITY_API_VERSION).pc
endif

if WITH_INFINOTED
SUBDIRS += infinoted
endif

SUBDIRS += pixmaps test docs po

DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc
