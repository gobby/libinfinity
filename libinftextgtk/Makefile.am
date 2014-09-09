# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinftextgtk-0.7.la

cppflags = \
	-I$(top_srcdir) \
	$(inftextgtk_CFLAGS) \
	$(inftext_CFLAGS) \
	$(infinity_CFLAGS)

ldflags = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libadd = \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(inftextgtk_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

headers = \
	inf-text-gtk-buffer.h \
	inf-text-gtk-hue-chooser.h \
	inf-text-gtk-view.h \
	inf-text-gtk-viewport.h

sources = \
	inf-text-gtk-buffer.c \
	inf-text-gtk-hue-chooser.c \
	inf-text-gtk-view.c \
	inf-text-gtk-viewport.c

libinftextgtk_0_7_la_CPPFLAGS = $(cppflags)
libinftextgtk_0_7_la_LDFLAGS = $(ldflags)
libinftextgtk_0_7_la_LIBADD = $(libadd)
libinftextgtk_0_7_la_HEADERS = $(headers)
libinftextgtk_0_7_la_SOURCES = $(sources)
libinftextgtk_0_7_ladir = \
	$(includedir)/libinftextgtk-$(LIBINFINITY_API_VERSION)/libinftextgtk

if HAVE_INTROSPECTION
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS = InfTextGtk-0.7.gir
INTROSPECTION_SCANNER_ENV = CC="${CC}"
INTROSPECTION_COMPILER_ARGS = \
	--includedir=${top_builddir}/libinfinity \
	--includedir=${top_builddir}/libinftext \
	--includedir=${top_builddir}/libinfgtk

InfTextGtk-0.7.gir: libinftextgtk-0.7.la

InfTextGtk_0_7_gir_SCANNERFLAGS = \
	-I${top_srcdir} \
	--add-include-path=$(top_builddir)/libinfinity \
	--add-include-path=$(top_builddir)/libinftext \
	--add-include-path=$(top_builddir)/libinfgtk \
	-n InfTextGtk \
	--identifier-prefix=InfTextGtk \
	--warn-all

InfTextGtk_0_7_gir_NAMESPACE = InfTextGtk
InfTextGtk_0_7_gir_VERSION = 0.7
InfTextGtk_0_7_gir_CFLAGS = $(inftextgtk_CFLAGS) $(inftext_CFLAGS) $(infinity_CFLAGS)
InfTextGtk_0_7_gir_LIBS = libinftextgtk-0.7.la
InfTextGtk_0_7_gir_FILES = \
	$(libinftextgtk_0_7_la_SOURCES) \
	$(libinftextgtk_0_7_la_HEADERS)

InfTextGtk_0_7_gir_INCLUDES = InfText-0.7 InfGtk-0.7 Gtk-3.0
InfTextGtk_0_7_gir_PACKAGES = gtk+-3.0
InfTextGtk_0_7_gir_EXPORT_PACKAGES = libinftextgtk-0.7

girdir = $(datadir)/gir-1.0
gir_DATA = InfTextGtk-0.7.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = InfTextGtk-0.7.typelib

CLEANFILES = \
	$(gir_DATA) \
	$(typelib_DATA)
endif
