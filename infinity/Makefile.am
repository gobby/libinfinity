lib_LTLIBRARIES = libinfinity-1.0.la

libinfinity_1_0_la_CPPFLAGS = \
	-I${top_srcdir} \
	$(glib_CFLAGS) \
	$(gobject_CFLAGS) \
	$(libgnetwork_CFLAGS) \
	$(libxml2_CFLAGS)

libinfinity_1_0_la_LIBADD = \
	$(glib_LIBS) \
	$(gobject_LIBS) \
	$(libgnetwork_LIBS) \
	$(libxml2_LIBS)

dist_pkgdata_DATA = inf-marshal.in

inf-marshal.h: inf-marshal.in
	glib-genmarshal --header --prefix=inf_marshal $< > $@

inf-marshal.c: inf-marshal.in
	glib-genmarshal --body --prefix=inf_marshal $< > $@

BUILT_SOURCES = inf-marshal.h inf-marshal.c
CLEANFILES = inf-marshal.h inf-marshal.c

libinfinity_1_0_ladir = infinity

libinfinity_1_0_la_HEADERS = \
	inf-buffer.h \
	inf-config.h \
	inf-connection-manager.h \
	inf-net-object.h \
	inf-stroke.h \
	inf-xml-stream.h

libinfinity_1_0_la_SOURCES = \
	inf-buffer.c \
	inf-connection-manager.c \
	inf-net-object.c \
	inf-stroke.c \
	inf-xml-stream.c
