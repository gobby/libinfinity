# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinftext-0.7.la

libinftext_0_7_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(inftext_CFLAGS) \
	$(infinity_CFLAGS)

libinftext_0_7_la_LDFLAGS = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libinftext_0_7_la_LIBADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinftext_0_7_ladir = \
	$(includedir)/libinftext-$(LIBINFINITY_API_VERSION)/libinftext

libinftext_0_7_la_HEADERS = \
	inf-text-buffer.h \
	inf-text-chunk.h \
	inf-text-default-buffer.h \
	inf-text-default-delete-operation.h \
	inf-text-default-insert-operation.h \
	inf-text-delete-operation.h \
	inf-text-filesystem-format.h \
	inf-text-fixline-buffer.h \
	inf-text-insert-operation.h \
	inf-text-move-operation.h \
	inf-text-operations.h \
	inf-text-remote-delete-operation.h \
	inf-text-session.h \
	inf-text-undo-grouping.h \
	inf-text-user.h

libinftext_0_7_la_SOURCES = \
	inf-text-buffer.c \
	inf-text-chunk.c \
	inf-text-default-buffer.c \
	inf-text-default-delete-operation.c \
	inf-text-default-insert-operation.c \
	inf-text-delete-operation.c \
	inf-text-filesystem-format.c \
	inf-text-fixline-buffer.c \
	inf-text-insert-operation.c \
	inf-text-move-operation.c \
	inf-text-remote-delete-operation.c \
	inf-text-session.c \
	inf-text-undo-grouping.c \
	inf-text-user.c

if HAVE_INTROSPECTION
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS = InfText-0.7.gir
INTROSPECTION_SCANNER_ENV = CC="${CC}"
INTROSPECTION_COMPILER_ARGS = \
	--includedir=${top_builddir}/libinfinity

InfText-0.7.gir: libinftext-0.7.la

InfText_0_7_gir_SCANNERFLAGS = \
	-I${top_srcdir} \
	--add-include-path=$(top_builddir)/libinfinity \
	-n InfText \
	--identifier-prefix=InfText \
	--warn-all

InfText_0_7_gir_NAMESPACE = InfText
InfText_0_7_gir_VERSION = 0.7
InfText_0_7_gir_CFLAGS = $(inftext_CFLAGS) $(infinity_CFLAGS)
InfText_0_7_gir_LIBS = libinftext-0.7.la
InfText_0_7_gir_FILES = \
	$(libinftext_0_7_la_SOURCES) \
	$(libinftext_0_7_la_HEADERS)

InfText_0_7_gir_INCLUDES = Infinity-0.7
InfText_0_7_gir_PACKAGES = gobject-2.0
InfText_0_7_gir_EXPORT_PACKAGES = libinftext-0.7

girdir = $(datadir)/gir-1.0
gir_DATA = InfText-0.7.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = InfText-0.7.typelib

CLEANFILES = \
	$(gir_DATA) \
	$(typelib_DATA)
endif
