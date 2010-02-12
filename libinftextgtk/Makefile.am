# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinftextgtk-0.5.la

libinftextgtk_0_5_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(inftextgtk_CFLAGS) \
	$(inftext_CFLAGS) \
	$(infinity_CFLAGS)

libinftextgtk_0_5_la_LDFLAGS = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libinftextgtk_0_5_la_LIBADD = \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(inftextgtk_LIBS) \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinftextgtk_0_5_ladir = \
	$(includedir)/libinftextgtk-$(LIBINFINITY_API_VERSION)/libinftextgtk

libinftextgtk_0_5_la_HEADERS = \
	inf-text-gtk-buffer.h \
	inf-text-gtk-hue-chooser.h \
	inf-text-gtk-view.h

libinftextgtk_0_5_la_SOURCES = \
	inf-text-gtk-buffer.c \
	inf-text-gtk-hue-chooser.c \
	inf-text-gtk-view.c
