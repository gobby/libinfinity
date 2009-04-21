SUBDIRS = communication common adopted server client .

# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinfinity-0.3.la

libinfinity_0_3_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinity_CFLAGS)

libinfinity_0_3_la_LDFLAGS = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libinfinity_0_3_la_LIBADD = \
	server/libinfinity_server.la \
	client/libinfinity_client.la \
	adopted/libinfinity_adopted.la \
	common/libinfinity_common.la \
	communication/libinfinity_communication.la \
	$(infinity_LIBS) \
	$(glib_LIBS) \
	$(avahi_LIBS)

inf-marshal.h: inf-marshal.in
	glib-genmarshal --header --prefix=inf_marshal $< > $@

inf-marshal.c: inf-marshal.in
	glib-genmarshal --body --prefix=inf_marshal $< > $@

BUILT_SOURCES = inf-marshal.h inf-marshal.c
CLEANFILES = inf-marshal.h inf-marshal.c

libinfinity_0_3_ladir = \
	$(includedir)/libinfinity-$(LIBINFINITY_API_VERSION)/libinfinity

libinfinity_0_3_la_HEADERS = \
	inf-config.h

noinst_HEADERS = \
	inf-dll.h \
	inf-i18n.h \
	inf-marshal.h

libinfinity_0_3_la_SOURCES = \
	inf-dll.c \
	inf-i18n.c \
	inf-marshal.c

EXTRA_DIST = inf-marshal.in
