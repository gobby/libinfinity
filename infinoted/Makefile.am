SUBDIRS = note-plugins

# TODO: Find a way to have the version number set automatically.
bin_PROGRAMS = infinoted-0.5

plugin_path = infinoted-$(LIBINFINITY_API_VERSION)/note-plugins

infinoted_0_5_CPPFLAGS = \
	-I${top_srcdir} \
	$(infinoted_CFLAGS) \
	$(infinity_CFLAGS) \
	-DPLUGIN_LIBPATH=\"${libdir}\" \
	-DPLUGIN_BASEPATH=\"${plugin_path}\" \
	-DLOCALSTATEDIR=\"${localstatedir}\" \
	-DLIBINFINITY_API_VERSION=\"$(LIBINFINITY_API_VERSION)\"

infinoted_0_5_LDADD = \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(infinity_LIBS) \
	$(libdaemon_LIBS)

infinoted_0_5_SOURCES = \
	infinoted-autosave.c \
	infinoted-config-reload.c \
	infinoted-creds.c \
	infinoted-dh-params.c \
	infinoted-directory-sync.c \
	infinoted-main.c \
	infinoted-note-plugin.c \
	infinoted-options.c \
	infinoted-record.c \
	infinoted-run.c \
	infinoted-signal.c \
	infinoted-startup.c \
	infinoted-util.c

noinst_HEADERS = \
	infinoted-autosave.h \
	infinoted-config-reload.h \
	infinoted-creds.h \
	infinoted-dh-params.h \
	infinoted-directory-sync.h \
	infinoted-note-plugin.h \
	infinoted-options.h \
	infinoted-record.h \
	infinoted-run.h \
	infinoted-signal.h \
	infinoted-startup.h \
	infinoted-util.h

# Create pid file directory
pidfiledir = ${localstatedir}/run/infinoted-$(LIBINFINITY_API_VERSION)
pidfile_DATA =
