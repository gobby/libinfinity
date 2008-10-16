methods_LTLIBRARIES = libinf-method-local-central.la

# TODO: Actually, I would like that path to be
# ${libdir}/libinfinity-1.0/method-plugins. However, if I do choose something
# else than this, then libtool complains:
#
# libtool: install: error: cannot install `libinf-method-local-central.la' to
# a directory not ending in /usr/local/lib/infinote/method-plugins
#
# I have really no idea what the idea behind that behaviour is, or how to
# fix it.
#
# Remember to adapt libinfinity/common/Makefile.am if you change this.
methodsdir = ${libdir}/infinote/method-plugins

# TODO: Actually we only want to install the .so file, and nothing else.
# I don't know how to achieve this, though.

libinf_method_local_central_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinity_CFLAGS)

libinf_method_local_central_la_LDFLAGS = \
	-avoid-version -module -no-undefined

libinf_method_local_central_la_LIBADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la \
	$(infinity_LIBS)

libinf_method_local_central_la_SOURCES = \
	inf-method-local-central.c

noinst_HEADERS =
