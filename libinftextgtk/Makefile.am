lib_LTLIBRARIES = libinftextgtk-1.0.la

AM_CPPFLAGS = -I${top_srcdir}

libinftextgtk_1_0_la_CPPFLAGS = \
	$(inftextgtk_CFLAGS) \
	$(inftext_CFLAGS) \
	$(infinity_CFLAGS)

libinftextgtk_1_0_la_LDFLAGS = \
	-no-undefined

libinftextgtk_1_0_la_LIBADD = \
	${top_srcdir}/libinftext/libinftext-1.0.la \
	${top_srcdir}/libinfinity/libinfinity-1.0.la \
	$(inftextgtk_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinftextgtk_1_0_ladir = $(includedir)/libinftextgtk-1.0/libinftextgtk

libinftextgtk_1_0_la_HEADERS = \
	inf-text-gtk-buffer.h \
	inf-text-gtk-hue-chooser.h

libinftextgtk_1_0_la_SOURCES = \
	inf-text-gtk-buffer.c \
	inf-text-gtk-hue-chooser.c
