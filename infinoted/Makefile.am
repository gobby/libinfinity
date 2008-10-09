SUBDIRS = note-plugins

bin_PROGRAMS = infinoted

# TODO: Make these configurable at configure time
plugin_path = ${libdir}/infinoted/note-plugins

infinoted_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_builddir} \
	$(infinity_CFLAGS) \
	$(infinoted_CFLAGS) \
	-DPLUGIN_PATH=\"${plugin_path}\"

infinoted_LDADD = \
	$(infinity_LIBS) \
	$(infinoted_LIBS) \
	${top_builddir}/libinfinity/libinfinity-1.0.la

infinoted_SOURCES = \
	infinoted-creds.c \
	infinoted-main.c \
	infinoted-note-plugin.c \
	infinoted-options.c \
	infinoted-run.c \
	infinoted-signal.c \
	infinoted-startup.c

noinst_HEADERS = \
	infinoted-creds.h \
	infinoted-note-plugin.h \
	infinoted-options.h \
	infinoted-run.h \
	infinoted-signal.h \
	infinoted-startup.h
