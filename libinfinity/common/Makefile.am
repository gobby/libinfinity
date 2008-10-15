AM_CPPFLAGS = -I${top_srcdir}

methods_base = infinote/method-plugins

libinfinity_common_la_CPPFLAGS = \
	$(infinity_CFLAGS) \
	$(avahi_CFLAGS) \
	-DMETHODS_LIBDIR=\"${libdir}\" \
	-DMETHODS_BASEDIR=\"${methods_base}\"

noinst_LTLIBRARIES = libinfinity_common.la

libinfinity_common_ladir = $(includedir)/libinfinity-1.0/libinfinity/common

libinfinity_common_la_SOURCES = \
	inf-buffer.c \
	inf-central-method.c \
	inf-certificate-chain.c \
	inf-cert-util.c \
	inf-connection-manager.c \
	inf-discovery-avahi.c \
	inf-discovery.c \
	inf-error.c \
	inf-init.c \
	inf-io.c \
	inf-ip-address.c \
	inf-local-publisher.c \
	inf-method-manager.c \
	inf-net-object.c \
	inf-session.c \
	inf-simulated-connection.c \
	inf-standalone-io.c \
	inf-tcp-connection.c \
	inf-user.c \
	inf-user-table.c \
	inf-xml-connection.c \
	inf-xml-util.c \
	inf-xmpp-connection.c \
	inf-xmpp-manager.c

libinfinity_common_la_HEADERS = \
	inf-buffer.h \
	inf-central-method.h \
	inf-certificate-chain.h \
	inf-cert-util.h \
	inf-connection-manager.h \
	inf-discovery.h \
	inf-discovery-avahi.h \
	inf-error.h \
	inf-init.h \
	inf-io.h \
	inf-ip-address.h \
	inf-local-publisher.h \
	inf-method-manager.h \
	inf-net-object.h \
	inf-session.h \
	inf-simulated-connection.h \
	inf-standalone-io.h \
	inf-tcp-connection.h \
	inf-user.h \
	inf-user-table.h \
	inf-xml-connection.h \
	inf-xml-util.h \
	inf-xmpp-connection.h \
	inf-xmpp-manager.h
