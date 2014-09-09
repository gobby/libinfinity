SUBDIRS = . plugins

# TODO: Find a way to have the version number set automatically.
bin_PROGRAMS = infinoted-0.7
dist_man1_MANS = infinoted-0.7.man

plugin_path = infinoted-$(LIBINFINITY_API_VERSION)/plugins

# Compile the plugin manager as a shared library:
# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinfinoted-plugin-manager-0.7.la

libinfinoted_plugin_manager_0_7_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinoted_CFLAGS) \
	$(infinity_CFLAGS) \
	-DPLUGIN_LIBPATH=\"${libdir}\" \
	-DPLUGIN_PATH=\"${plugin_path}\" \
	-DLOCALSTATEDIR=\"${localstatedir}\" \
	-DLIBINFINITY_API_VERSION=\"$(LIBINFINITY_API_VERSION)\"

libinfinoted_plugin_manager_0_7_la_LDFLAGS = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libinfinoted_plugin_manager_0_7_la_LIBADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(infinity_LIBS) \
	$(libdaemon_LIBS)

libinfinoted_plugin_manager_0_7_la_SOURCES = \
	infinoted-log.c \
	infinoted-parameter.c \
	infinoted-plugin-manager.c \
	infinoted-util.c

libinfinoted_plugin_manager_0_7_la_HEADERS = \
	infinoted-log.h \
	infinoted-parameter.h \
	infinoted-plugin-manager.h \
	infinoted-util.h

libinfinoted_plugin_manager_0_7_ladir = \
	$(includedir)/libinfinoted-plugin-manager-$(LIBINFINITY_API_VERSION)/infinoted

# Compile the rest of infinoted as an executable binary
infinoted_0_7_CPPFLAGS = \
	-I${top_srcdir} \
	$(infinoted_CFLAGS) \
	$(infinity_CFLAGS) \
	-DPLUGIN_LIBPATH=\"${libdir}\" \
	-DPLUGIN_PATH=\"${plugin_path}\" \
	-DLOCALSTATEDIR=\"${localstatedir}\" \
	-DLIBINFINITY_API_VERSION=\"$(LIBINFINITY_API_VERSION)\"

infinoted_0_7_LDADD = \
	libinfinoted-plugin-manager-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(infinity_LIBS) \
	$(libdaemon_LIBS)

infinoted_0_7_SOURCES = \
	infinoted-config-reload.c \
	infinoted-dh-params.c \
	infinoted-main.c \
	infinoted-options.c \
	infinoted-pam.c \
	infinoted-run.c \
	infinoted-signal.c \
	infinoted-startup.c

noinst_HEADERS = \
	infinoted-config-reload.h \
	infinoted-dh-params.h \
	infinoted-options.h \
	infinoted-pam.h \
	infinoted-run.h \
	infinoted-signal.h \
	infinoted-startup.h

# Create pid file directory
pidfiledir = ${localstatedir}/run/infinoted-$(LIBINFINITY_API_VERSION)
pidfile_DATA =

if HAVE_INTROSPECTION
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS = Infinoted-0.7.gir
INTROSPECTION_SCANNER_ENV = CC="${CC}"
INTROSPECTION_COMPILER_ARGS = \
	--includedir=${top_builddir}/libinfinity \
	--includedir=${top_builddir}/libinftext \
	--includedir=${top_builddir}/libinfgtk

Infinoted-0.7.gir: libinfinoted-plugin-manager-0.7.la

Infinoted_0_7_gir_SCANNERFLAGS = \
	-I${top_srcdir} \
	--add-include-path=$(top_builddir)/libinfinity \
	-n Infinoted \
	--identifier-prefix=Infinoted \
	--warn-all

Infinoted_0_7_gir_NAMESPACE = Infinoted
Infinoted_0_7_gir_VERSION = 0.7
Infinoted_0_7_gir_CFLAGS = $(infinoted_CFLAGS) $(infinity_CFLAGS)
Infinoted_0_7_gir_LIBS = libinfinoted-plugin-manager-0.7.la
Infinoted_0_7_gir_FILES = \
	$(libinfinoted_plugin_manager_0_7_la_SOURCES) \
	$(libinfinoted_plugin_manager_0_7_la_HEADERS)

Infinoted_0_7_gir_INCLUDES = Infinity-0.7 Infinityd-0.7
Infinoted_0_7_gir_PACKAGES = gobject-2.0
Infinoted_0_7_gir_EXPORT_PACKAGES = libinfinoted-plugin-manager-0.7

girdir = $(datadir)/gir-1.0
gir_DATA = Infinoted-0.7.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = Infinoted-0.7.typelib

CLEANFILES = \
	$(gir_DATA) \
	$(typelib_DATA)
endif
