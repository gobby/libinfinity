plugin_LTLIBRARIES = libinfd-note-plugin-text.la
plugindir = ${libdir}/infinoted/note-plugins

# TODO: Actually we only want to install the .so file, and nothing else.
# I don't know how to achieve this, though.

libinfd_note_plugin_text_la_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_builddir} \
	$(infinity_CFLAGS) \
	$(infinoted_CFLAGS) \
	$(inftext_CFLAGS)

libinfd_note_plugin_text_la_LIBADD = \
	$(infinity_LIBS) \
	$(infinoted_LIBS) \
	$(inftext_LIBS) \
	${top_builddir}/libinfinity/libinfinity-1.0.la \
	${top_builddir}/libinftext/libinftext-1.0.la

libinfd_note_plugin_text_la_SOURCES = \
	infd-note-plugin-text.c

noinst_HEADERS =
