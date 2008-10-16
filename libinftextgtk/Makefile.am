lib_LTLIBRARIES = libinftextgtk-1.0.la

libinftextgtk_1_0_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(inftextgtk_CFLAGS) \
	$(inftext_CFLAGS) \
	$(infinity_CFLAGS)

libinftextgtk_1_0_la_LDFLAGS = \
	-no-undefined

libinftextgtk_1_0_la_LIBADD = \
	${top_builddir}/libinftext/libinftext-1.0.la \
	${top_builddir}/libinfinity/libinfinity-1.0.la \
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
