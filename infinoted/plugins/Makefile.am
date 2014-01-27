plugin_LTLIBRARIES = \
	libinfinoted-plugin-autosave.la \
	libinfinoted-plugin-directory-sync.la \
	libinfinoted-plugin-linekeeper.la \
	libinfinoted-plugin-logging.la \
	libinfinoted-plugin-record.la \
	libinfinoted-plugin-traffic-logging.la \
	libinfinoted-plugin-transformation-protection.la

plugindir = ${libdir}/infinoted-$(LIBINFINITY_API_VERSION)/plugins

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinoted_CFLAGS) \
	$(inftext_CFLAGS) \
	$(infinity_CFLAGS)

AM_LDFLAGS = \
	-avoid-version -module -no-undefined

libinfinoted_plugin_autosave_la_LIBADD = \
	${top_builddir}/infinoted/libinfinoted-plugin-manager-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinfinoted_plugin_directory_sync_la_LIBADD = \
	${top_builddir}/infinoted/libinfinoted-plugin-manager-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinfinoted_plugin_linekeeper_la_LIBADD = \
	${top_builddir}/infinoted/libinfinoted-plugin-manager-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinfinoted_plugin_logging_la_LIBADD = \
	${top_builddir}/infinoted/libinfinoted-plugin-manager-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinfinoted_plugin_record_la_LIBADD = \
	${top_builddir}/infinoted/libinfinoted-plugin-manager-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinfinoted_plugin_transformation_protection_la_LIBADD = \
	${top_builddir}/infinoted/libinfinoted-plugin-manager-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinfinoted_plugin_autosave_la_SOURCES = \
	infinoted-plugin-autosave.c

libinfinoted_plugin_directory_sync_la_SOURCES = \
	infinoted-plugin-directory-sync.c

libinfinoted_plugin_linekeeper_la_SOURCES = \
	infinoted-plugin-linekeeper.c

libinfinoted_plugin_logging_la_SOURCES = \
	infinoted-plugin-logging.c

libinfinoted_plugin_record_la_SOURCES = \
	infinoted-plugin-record.c

libinfinoted_plugin_traffic_logging_la_SOURCES = \
	infinoted-plugin-traffic-logging.c

libinfinoted_plugin_transformation_protection_la_SOURCES = \
	infinoted-plugin-transformation-protection.c
