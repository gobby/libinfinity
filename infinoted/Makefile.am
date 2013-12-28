SUBDIRS = . plugins note-plugins

# TODO: Find a way to have the version number set automatically.
bin_PROGRAMS = infinoted-0.6
dist_man1_MANS = infinoted-0.6.man

plugin_path = infinoted-$(LIBINFINITY_API_VERSION)/plugins
note_plugin_path = infinoted-$(LIBINFINITY_API_VERSION)/note-plugins

# Compile the plugin manager as a shared library:
# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinfinoted-plugin-manager-0.6.la

libinfinoted_plugin_manager_0_6_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinoted_CFLAGS) \
	$(infinity_CFLAGS)
	-DPLUGIN_LIBPATH=\"${libdir}\" \
	-DPLUGIN_PATH=\"${plugin_path}\" \
	-DNOTE_PLUGIN_PATH=\"${note_plugin_path}\" \
	-DLOCALSTATEDIR=\"${localstatedir}\" \
	-DLIBINFINITY_API_VERSION=\"$(LIBINFINITY_API_VERSION)\"

libinfinoted_plugin_manager_0_6_la_LDFLAGS = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libinfinoted_plugin_manager_0_6_la_LIBADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(infinity_LIBS)

libinfinoted_plugin_manager_0_6_la_SOURCES = \
	infinoted-parameter.h \
	infinoted-parameter.c \
	infinoted-plugin-manager.h \
	infinoted-plugin-manager.c

# Compile the rest of infinoted as an executable binary
infinoted_0_6_CPPFLAGS = \
	-I${top_srcdir} \
	$(infinoted_CFLAGS) \
	$(infinity_CFLAGS) \
	-DPLUGIN_LIBPATH=\"${libdir}\" \
	-DPLUGIN_PATH=\"${plugin_path}\" \
	-DNOTE_PLUGIN_PATH=\"${note_plugin_path}\" \
	-DLOCALSTATEDIR=\"${localstatedir}\" \
	-DLIBINFINITY_API_VERSION=\"$(LIBINFINITY_API_VERSION)\"

infinoted_0_6_LDADD = \
	${top_builddir}/infinoted/libinfinoted-plugin-manager-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(infinity_LIBS) \
	$(libdaemon_LIBS)

infinoted_0_6_SOURCES = \
	infinoted-autosave.c \
	infinoted-config-reload.c \
	infinoted-dh-params.c \
	infinoted-directory-sync.c \
	infinoted-log.c \
	infinoted-main.c \
	infinoted-note-plugin.c \
	infinoted-options.c \
	infinoted-pam.c \
	infinoted-record.c \
	infinoted-run.c \
	infinoted-signal.c \
	infinoted-startup.c \
	infinoted-traffic-logger.c \
	infinoted-transformation-protector.c \
	infinoted-util.c

noinst_HEADERS = \
	infinoted-autosave.h \
	infinoted-config-reload.h \
	infinoted-dh-params.h \
	infinoted-directory-sync.h \
	infinoted-log.h \
	infinoted-note-plugin.h \
	infinoted-options.h \
	infinoted-pam.h \
	infinoted-record.h \
	infinoted-run.h \
	infinoted-signal.h \
	infinoted-startup.h \
	infinoted-traffic-logger.h \
	infinoted-transformation-protector.h \
	infinoted-util.h

# Create pid file directory
pidfiledir = ${localstatedir}/run/infinoted-$(LIBINFINITY_API_VERSION)
pidfile_DATA =
