lib_LTLIBRARIES = libinftextgtk-1.0.la

INCLUDES = -I${top_srcdir}

libinftextgtk_1_0_la_CPPFLAGS = \
	$(infinity_CFLAGS) \
	$(inftext_CFLAGS) \
	$(inftextgtk_CFLAGS)

libinftextgtk_1_0_la_LIBADD = \
	${top_srcdir}/libinfinity/libinfinity-1.0.la \
	${top_srcdir}/libinftext/libinftext-1.0.la \
	$(infinity_LIBS) \
	$(inftext_LIBS) \
	$(inftextgtk_LIBS)

libinftextgtk_1_0_ladir = $(includedir)/libinftextgtk-1.0/libinftextgtk

libinftextgtk_1_0_la_HEADERS = \
	inf-text-gtk-buffer.h

libinftextgtk_1_0_la_SOURCES = \
	inf-text-gtk-buffer.c
