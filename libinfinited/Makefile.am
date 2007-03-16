lib_LTLIBRARIES = libinfinited-1.0.la

libinfinited_1_0_la_CPPFLAGS = \
	-I${top_srcdir} \
	$(glib_CFLAGS) \
	$(gobject_CFLAGS) \
	$(libxml2_CFLAGS)

libinfinited_1_0_la_LIBADD = \
	$(glib_LIBS) \
	$(gobject_LIBS) \
	$(libxml2_LIBS) \
	${top_builddir}/libinfinity/libinfinity-1.0.la

dist_pkgdata_DATA = infd-marshal.in

infd-marshal.h: infd-marshal.in
	glib-genmarshal --header --prefix=infd_marshal $< > $@

infd-marshal.c: infd-marshal.in
	glib-genmarshal --body --prefix=infd_marshal $< > $@

BUILT_SOURCES = infd-marshal.h infd-marshal.c
CLEANFILES = infd-marshal.h infd-marshal.c

libinfinited_1_0_ladir = libinfinited

libinfinited_1_0_la_HEADERS = \
	infd-directory.h \
	infd-marshal.h \
	infd-note-plugin.h \
	infd-session.h \
	infd-storage.h

libinfinited_1_0_la_SOURCES = \
	infd-directory.c \
	infd-marshal.c \
	infd-session.c \
	infd-storage.c
