SUBDIRS = util session cleanup

AM_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_builddir} \
	${infinity_CFLAGS}

if WITH_INFGTK
AM_CPPFLAGS += \
	${infgtk_CFLAGS}
endif

if WITH_INFTEXTGTK
AM_CPPFLAGS += \
	${inftextgtk_CFLAGS}
endif

noinst_PROGRAMS = inf-test-tcp-connection inf-test-xmpp-connection \
	inf-test-tcp-server inf-test-daemon inf-test-browser \
	inf-test-state-vector inf-test-chunk inf-test-text-operations \
	inf-test-text-session inf-test-text-cleanup

if WITH_INFTEXTGTK
noinst_PROGRAMS += inf-test-gtk-browser
endif

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

inf_test_state_vector_SOURCES = \
	inf-test-state-vector.c

inf_test_state_vector_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la

inf_test_chunk_SOURCES = \
	inf-test-chunk.c

inf_test_chunk_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la \
	${top_builddir}/libinftext/libinftext-1.0.la

inf_test_text_operations_SOURCES = \
	inf-test-text-operations.c

inf_test_text_operations_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la \
	${top_builddir}/libinftext/libinftext-1.0.la

inf_test_text_session_SOURCES = \
	inf-test-text-session.c

inf_test_text_session_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la \
	${top_builddir}/libinftext/libinftext-1.0.la \
	util/libinftestutil.a

inf_test_text_cleanup_SOURCES = \
	inf-test-text-cleanup.c

inf_test_text_cleanup_LDADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la \
	${top_builddir}/libinftext/libinftext-1.0.la \
	util/libinftestutil.a

if WITH_INFTEXTGTK
inf_test_gtk_browser_SOURCES = \
	inf-test-gtk-browser.c

inf_test_gtk_browser_LDADD = \
	${infgtk_LIBS} \
	${top_builddir}/libinfinity/libinfinity-1.0.la \
	${top_builddir}/libinftext/libinftext-1.0.la \
	${top_builddir}/libinfgtk/libinfgtk-1.0.la \
	${top_builddir}/libinftextgtk/libinftextgtk-1.0.la
endif
