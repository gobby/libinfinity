bin_PROGRAMS = infinoted

# TODO: Make these configurable at configure time
default_keypath = ${sysconfdir}/infinote/key.pem
default_certpath = ${sysconfdir}/infinote/cert.pem

infinoted_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_builddir} \
	$(infinity_CFLAGS) \
	-DDEFAULT_KEYPATH=\"${default_keypath}\" \
	-DDEFAULT_CERTPATH=\"${default_certpath}\"

infinoted_LDADD = \
	$(infinity_CFLAGS) \
	${top_builddir}/libinfinity/libinfinity-1.0.la

infinoted_SOURCES = \
	infinoted-creds.c \
	infinoted-main.c

noinst_HEADERS = \
	infinoted-creds.h
