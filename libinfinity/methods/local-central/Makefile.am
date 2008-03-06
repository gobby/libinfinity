plugin_LTLIBRARIES = libinf-method-local-central.la
plugindir = ${libdir}/infinote/method-plugins

# TODO: Actually we only want to install the .so file, and nothing else.
# I don't know how to achieve this, though.

libinf_method_local_central_la_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_builddir} \
	$(infinity_CFLAGS)

libinf_method_local_central_la_LIBADD = \
	$(infinity_LIBS) \
	${top_builddir}/libinfinity/libinfinity-1.0.la

libinf_method_local_central_la_SOURCES = \
	inf-method-local-central.c

noinst_HEADERS =
