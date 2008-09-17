SUBDIRS = common adopted server client . methods

lib_LTLIBRARIES = libinfinity-1.0.la

AM_CPPFLAGS = -I${top_srcdir}

libinfinity_1_0_la_CPPFLAGS = \
	$(infinity_CFLAGS)

libinfinity_1_0_la_LIBADD = \
	$(glib_LIBS) \
	$(infinity_LIBS) \
	$(avahi_LIBS) \
	common/libinfinity_common.la \
	adopted/libinfinity_adopted.la \
	server/libinfinity_server.la \
	client/libinfinity_client.la

dist_pkgdata_DATA = inf-marshal.in

inf-marshal.h: inf-marshal.in
	glib-genmarshal --header --prefix=inf_marshal $< > $@

inf-marshal.c: inf-marshal.in
	glib-genmarshal --body --prefix=inf_marshal $< > $@

BUILT_SOURCES = inf-marshal.h inf-marshal.c
CLEANFILES = inf-marshal.h inf-marshal.c

libinfinity_1_0_ladir = $(includedir)/libinfinity-1.0/libinfinity

libinfinity_1_0_la_HEADERS = \
	inf-config.h

noinst_HEADERS = \
	inf-i18n.h \
	inf-marshal.h

libinfinity_1_0_la_SOURCES = \
	inf-i18n.c \
	inf-marshal.c
