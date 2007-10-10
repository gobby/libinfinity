bin_PROGRAMS = infinoted

# TODO: Make these configurable at configure time
default_keypath = ${sysconfdir}/infinote/key.pem
default_certpath = ${sysconfdir}/infinote/cert.pem
plugin_path = ${datadir}/infinote/plugins

infinoted_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_builddir} \
	$(infinity_CFLAGS) \
	$(infinoted_CFLAGS) \
	-DDEFAULT_KEYPATH=\"${default_keypath}\" \
	-DDEFAULT_CERTPATH=\"${default_certpath}\" \
	-DPLUGIN_PATH=\"${plugin_path}\"

infinoted_LDADD = \
	$(infinity_LIBS) \
	$(infinoted_LIBS) \
	${top_builddir}/libinfinity/libinfinity-1.0.la

infinoted_SOURCES = \
	infinoted-creds.c \
	infinoted-main.c \
	infinoted-note-plugin.c

noinst_HEADERS = \
	infinoted-creds.h \
	infinoted-note-plugin.h
