# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinfgtk-0.7.la

cppflags = \
	-I$(top_srcdir) \
	$(infgtk_CFLAGS) \
	$(infinity_CFLAGS)

ldflags = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libadd = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infgtk_LIBS) \
	$(infinity_LIBS)

headers = \
	inf-gtk-account-creation-dialog.h \
	inf-gtk-acl-sheet-view.h \
	inf-gtk-browser-model.h \
	inf-gtk-browser-model-filter.h \
	inf-gtk-browser-model-sort.h \
	inf-gtk-browser-store.h \
	inf-gtk-browser-view.h \
	inf-gtk-certificate-dialog.h \
	inf-gtk-certificate-manager.h \
	inf-gtk-certificate-view.h \
	inf-gtk-chat.h \
	inf-gtk-io.h \
	inf-gtk-permissions-dialog.h \
	inf-gtk-resources.h

sources = \
	inf-gtk-account-creation-dialog.c \
	inf-gtk-acl-sheet-view.c \
	inf-gtk-browser-model.c \
	inf-gtk-browser-model-filter.c \
	inf-gtk-browser-model-sort.c \
	inf-gtk-browser-store.c \
	inf-gtk-browser-view.c \
	inf-gtk-certificate-dialog.c \
	inf-gtk-certificate-manager.c \
	inf-gtk-certificate-view.c \
	inf-gtk-chat.c \
	inf-gtk-io.c \
	inf-gtk-permissions-dialog.c \
	inf-gtk-resources.c

resources = \
	resources/ui/infgtkaccountcreationdialog.ui \
	resources/ui/infgtkaclsheetview.ui \
	resources/ui/infgtkcertificatedialog.ui \
	resources/ui/infgtkcertificateview.ui \
	resources/ui/infgtkchat.ui \
	resources/ui/infgtkpermissionsdialog.ui

inf-gtk-resources.h: resources/infgtk.gresources.xml
	glib-compile-resources $< \
		--target=$@ --sourcedir=$(srcdir)/resources \
		--c-name _inf_gtk --generate-header --internal

inf-gtk-resources.c: resources/infgtk.gresources.xml $(resources)
	glib-compile-resources $< \
		--target=$@ --sourcedir=$(srcdir)/resources \
		--c-name _inf_gtk --generate-source --internal

BUILT_SOURCES = inf-gtk-resources.h inf-gtk-resources.c
CLEANFILES = inf-gtk-resources.h inf-gtk-resources.c

libinfgtk_0_7_la_CPPFLAGS = $(cppflags)
libinfgtk_0_7_la_LDFLAGS = $(ldflags)
libinfgtk_0_7_la_LIBADD = $(libadd)
libinfgtk_0_7_la_HEADERS = $(headers)
libinfgtk_0_7_la_SOURCES = $(sources)
libinfgtk_0_7_ladir = \
	$(includedir)/libinfgtk-$(LIBINFINITY_API_VERSION)/libinfgtk

if HAVE_INTROSPECTION
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS = InfGtk-0.7.gir
INTROSPECTION_SCANNER_ENV = CC="${CC}"
INTROSPECTION_COMPILER_ARGS = \
	--includedir=${top_builddir}/libinfinity

InfGtk-0.7.gir: libinfgtk-0.7.la

InfGtk_0_7_gir_SCANNERFLAGS = \
	-I${top_srcdir} \
	--add-include-path=$(top_builddir)/libinfinity \
	-n InfGtk \
	--identifier-prefix=InfGtk

InfGtk_0_7_gir_NAMESPACE = InfGtk
InfGtk_0_7_gir_VERSION = 0.7
InfGtk_0_7_gir_CFLAGS = $(infgtk_CFLAGS) $(infinity_CFLAGS)
InfGtk_0_7_gir_LIBS = libinfgtk-0.7.la
InfGtk_0_7_gir_FILES = \
	$(libinfgtk_0_7_la_SOURCES) \
	$(libinfgtk_0_7_la_HEADERS)

InfGtk_0_7_gir_INCLUDES = Infinity-0.7 Gtk-3.0
InfGtk_0_7_gir_PACKAGES = gtk+-3.0
InfGtk_0_7_gir_EXPORT_PACKAGES = libinfgtk-0.7

girdir = $(datadir)/gir-1.0
gir_DATA = InfGtk-0.7.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = InfGtk-0.7.typelib

CLEANFILES += \
	$(gir_DATA) \
	$(typelib_DATA)
endif
