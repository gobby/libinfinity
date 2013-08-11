plugin_LTLIBRARIES = libinfd-note-plugin-chat.la
plugindir = ${libdir}/infinoted-$(LIBINFINITY_API_VERSION)/note-plugins

libinfd_note_plugin_chat_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinoted_CFLAGS) \
	$(infinity_CFLAGS)

libinfd_note_plugin_chat_la_LDFLAGS = \
	-avoid-version -module -no-undefined

libinfd_note_plugin_chat_la_LIBADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infinoted_LIBS) \
	$(infinity_LIBS)

libinfd_note_plugin_chat_la_SOURCES = \
	infd-note-plugin-chat.c

noinst_HEADERS =
