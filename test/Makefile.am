AM_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_builddir} \
	${infinity_CFLAGS}

noinst_PROGRAMS = inf-test-tcp-connection inf-test-xmpp-connection \
	inf-test-tcp-server inf-test-daemon inf-test-browser

inf_test_tcp_connection_SOURCES = \
	inf-test-tcp-connection.c

inf_test_tcp_connection_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la

inf_test_xmpp_connection_SOURCES = \
	inf-test-xmpp-connection.c

inf_test_xmpp_connection_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la

inf_test_tcp_server_SOURCES = \
	inf-test-tcp-server.c

inf_test_tcp_server_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la

inf_test_daemon_SOURCES = \
	inf-test-daemon.c

inf_test_daemon_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la

inf_test_browser_SOURCES = \
	inf-test-browser.c

inf_test_browser_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la
