plugin_LTLIBRARIES = libinf-method-local-central.la
plugindir = ${libdir}/libinfinity-1.0/method-plugins

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
