AM_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_builddir} \
	${glib_CFLAGS} \
	${gobject_CFLAGS} \
	${libxml2_CFLAGS}

noinst_PROGRAMS = inf-test-tcp-connection

inf_test_tcp_connection_SOURCES = \
	inf-test-tcp-connection.c

inf_test_tcp_connection_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la
