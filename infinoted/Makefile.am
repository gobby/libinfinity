SUBDIRS = note-plugins

bin_PROGRAMS = infinoted

plugin_path = infinoted-1.0/note-plugins

infinoted_CPPFLAGS = \
	-I${top_srcdir} \
	$(infinoted_CFLAGS) \
	$(infinity_CFLAGS) \
	-DPLUGIN_LIBPATH=\"${libdir}\" \
	-DPLUGIN_BASEPATH=\"${plugin_path}\"

infinoted_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la \
	$(infinoted_LIBS) \
	$(infinity_LIBS)

infinoted_SOURCES = \
	infinoted-autosave.c \
	infinoted-creds.c \
	infinoted-main.c \
	infinoted-note-plugin.c \
	infinoted-options.c \
	infinoted-run.c \
	infinoted-signal.c \
	infinoted-startup.c

noinst_HEADERS = \
	infinoted-autosave.h \
	infinoted-creds.h \
	infinoted-note-plugin.h \
	infinoted-options.h \
	infinoted-run.h \
	infinoted-signal.h \
	infinoted-startup.h
