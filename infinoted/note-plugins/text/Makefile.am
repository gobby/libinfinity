plugin_LTLIBRARIES = libinfd-note-plugin-text.la
plugindir = ${libdir}/infinoted-1.0/note-plugins

libinfd_note_plugin_text_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinoted_CFLAGS) \
	$(inftext_CFLAGS) \
	$(infinity_CFLAGS)

libinfd_note_plugin_text_la_LDFLAGS = \
	-avoid-version -module -no-undefined

libinfd_note_plugin_text_la_LIBADD = \
	${top_builddir}/libinftext/libinftext-1.0.la \
	${top_builddir}/libinfinity/libinfinity-1.0.la \
	$(infinoted_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinfd_note_plugin_text_la_SOURCES = \
	infd-note-plugin-text.c

noinst_HEADERS =
