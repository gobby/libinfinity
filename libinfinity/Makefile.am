SUBDIRS = communication common adopted server client .

# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinfinity-0.5.la

libinfinity_0_5_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinity_CFLAGS)

libinfinity_0_5_la_LDFLAGS = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libinfinity_0_5_la_LIBADD = \
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

# rewrite the output to avoid a warning for casting gpointer to a function
# pointer type
inf-marshal.c: inf-marshal.in
	echo '#include <libinfinity/inf-marshal.h>' > $@
	glib-genmarshal --body --prefix=inf_marshal $< | perl -pe 's/^  callback = \((GMarshalFunc_[^)]+)\) \(marshal_data \? marshal_data : cc->callback\);/  callback = *($$1*) (marshal_data ? &marshal_data : &cc->callback);/' >> $@

BUILT_SOURCES = inf-marshal.h inf-marshal.c
CLEANFILES = inf-marshal.h inf-marshal.c

libinfinity_0_5_ladir = \
	$(includedir)/libinfinity-$(LIBINFINITY_API_VERSION)/libinfinity

libinfinity_0_5_la_HEADERS = \
	inf-config.h

noinst_HEADERS = \
	inf-dll.h \
	inf-i18n.h \
	inf-marshal.h \
	inf-signals.h

libinfinity_0_5_la_SOURCES = \
	inf-dll.c \
	inf-i18n.c \
	inf-marshal.c \
	inf-signals.c

EXTRA_DIST = inf-marshal.in
