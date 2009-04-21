noinst_LIBRARIES = libinftestutil.a

libinftestutil_a_CPPFLAGS = \
	-I${top_srcdir} \
	-I${top_builddir} \
	${infinity_CFLAGS} \
	${inftext_CFLAGS}

libinftestutil_a_SOURCES = \
	inf-test-util.c

noinst_HEADERS = \
	inf-test-util.h

libinftestutil_a_LIBADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la
