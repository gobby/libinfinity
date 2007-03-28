AM_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_builddir} \
	${infinity_CFLAGS}

noinst_PROGRAMS = inf-test-tcp-connection inf-test-xmpp-connection

inf_test_tcp_connection_SOURCES = \
	inf-test-tcp-connection.c

inf_test_tcp_connection_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la

inf_test_xmpp_connection_SOURCES = \
	inf-test-xmpp-connection.c

inf_test_xmpp_connection_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la
