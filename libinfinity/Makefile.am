lib_LTLIBRARIES = libinfinity-1.0.la

libinfinity_1_0_la_CPPFLAGS = \
	-I${top_srcdir} \
	$(glib_CFLAGS) \
	$(gobject_CFLAGS) \
	$(libxml2_CFLAGS) \
	$(gnutls_CFLAGS) \
	$(gsasl_CFLAGS)

libinfinity_1_0_la_LIBADD = \
	$(glib_LIBS) \
	$(gobject_LIBS) \
	$(libxml2_LIBS) \
	$(gnutls_LIBS) \
	$(gsasl_LIBS)

dist_pkgdata_DATA = inf-marshal.in

inf-marshal.h: inf-marshal.in
	glib-genmarshal --header --prefix=inf_marshal $< > $@

inf-marshal.c: inf-marshal.in
	glib-genmarshal --body --prefix=inf_marshal $< > $@

BUILT_SOURCES = inf-marshal.h inf-marshal.c
CLEANFILES = inf-marshal.h inf-marshal.c

libinfinity_1_0_ladir = libinfinity

libinfinity_1_0_la_HEADERS = \
	inf-buffer.h \
	inf-config.h \
	inf-connection-manager.h \
	inf-discovery.h \
	inf-discovery-info.h \
	inf-error.h \
	inf-ink-buffer.h \
	inf-ink-stroke.h \
	inf-io.h \
	inf-ip-address.h \
	inf-marshal.h \
	inf-net-object.h \
	inf-session.h \
	inf-standalone-io.h \
	inf-tcp-connection.h \
	inf-text-buffer.h \
	inf-user.h \
	inf-xml-connection.h \
	inf-xmpp-connection.h

libinfinity_1_0_la_SOURCES = \
	inf-buffer.c \
	inf-connection-manager.c \
	inf-discovery.c \
	inf-discovery-info.c \
	inf-error.c \
	inf-ink-buffer.c \
	inf-ink-stroke.c \
	inf-io.c \
	inf-ip-address.c \
	inf-marshal.c \
	inf-net-object.c \
	inf-session.c \
	inf-standalone-io.c \
	inf-tcp-connection.c \
	inf-text-buffer.c \
	inf-user.c \
	inf-xml-connection.c \
	inf-xmpp-connection.c 
