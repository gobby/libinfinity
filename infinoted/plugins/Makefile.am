plugin_LTLIBRARIES = libinfinoted-plugin-linekeeper.la
plugindir = ${libdir}/infinoted-$(LIBINFINITY_API_VERSION)/plugins

libinfinoted_plugin_linekeeper_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinoted_CFLAGS) \
	$(inftext_CFLAGS) \
	$(infinity_CFLAGS)

libinfinoted_plugin_linekeeper_la_LDFLAGS = \
	-avoid-version -module -no-undefined

libinfinoted_plugin_linekeeper_la_LIBADD = \
	${top_builddir}/infinoted/libinfinoted-plugin-manager-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinfinoted_plugin_linekeeper_la_SOURCES = \
	infinoted-plugin-linekeeper.c
