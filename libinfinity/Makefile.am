SUBDIRS = common server client

lib_LTLIBRARIES = libinfinity-1.0.la

INCLUDES = -I${top_srcdir}

libinfinity_1_0_la_CPPFLAGS = \
	$(infinity_CFLAGS)

libinfinity_1_0_la_LIBADD = \
	$(glib_LIBS) \
	$(infinity_LIBS) \
	common/libinfinity_common.la \
	server/libinfinity_server.la \
	client/libinfinity_client.la

dist_pkgdata_DATA = inf-marshal.in

inf-marshal.h: inf-marshal.in
	glib-genmarshal --header --prefix=inf_marshal $< > $@

inf-marshal.c: inf-marshal.in
	glib-genmarshal --body --prefix=inf_marshal $< > $@

BUILT_SOURCES = inf-marshal.h inf-marshal.c
CLEANFILES = inf-marshal.h inf-marshal.c

libinfinity_1_0_ladir = $(includedir)/libinfinity-1.0/infinity

# TODO: Do not install these two files
libinfinity_1_0_la_HEADERS = \
	inf-config.h \
	inf-marshal.h

libinfinity_1_0_la_SOURCES = \
	inf-marshal.c
