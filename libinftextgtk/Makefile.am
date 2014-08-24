# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinftextgtk-0.7.la

cppflags = \
	-I$(top_srcdir) \
	$(inftextgtk_CFLAGS) \
	$(inftext_CFLAGS) \
	$(infinity_CFLAGS)

ldflags = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libadd = \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(inftextgtk_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

headers = \
	inf-text-gtk-buffer.h \
	inf-text-gtk-hue-chooser.h \
	inf-text-gtk-view.h \
	inf-text-gtk-viewport.h

sources = \
	inf-text-gtk-buffer.c \
	inf-text-gtk-hue-chooser.c \
	inf-text-gtk-view.c \
	inf-text-gtk-viewport.c

libinftextgtk_0_7_la_CPPFLAGS = $(cppflags)
libinftextgtk_0_7_la_LDFLAGS = $(ldflags)
libinftextgtk_0_7_la_LIBADD = $(libadd)
libinftextgtk_0_7_la_HEADERS = $(headers)
libinftextgtk_0_7_la_SOURCES = $(sources)
libinftextgtk_0_7_ladir = \
	$(includedir)/libinftextgtk-$(LIBINFINITY_API_VERSION)/libinftextgtk
