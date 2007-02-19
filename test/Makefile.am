AM_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_builddir} \
	${glib_CFLAGS} \
	${gobject_CFLAGS} \
	${libxml2_CFLAGS}

noinst_PROGRAMS = inf-test-xml-stream

inf_test_xml_stream_SOURCES = \
	inf-test-xml-stream.c

inf_test_xml_stream_LDADD = \
	${top_builddir}/infinity/libinfinity-1.0.la
