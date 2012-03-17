# TODO: Find a way to have the version number set automatically.
if WITH_GTK3
lib_LTLIBRARIES = libinftextgtk3-0.6.la
else
lib_LTLIBRARIES = libinftextgtk-0.6.la
endif

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

if WITH_GTK3
libinftextgtk3_0_6_la_CPPFLAGS = $(cppflags)
libinftextgtk3_0_6_la_LDFLAGS = $(ldflags)
libinftextgtk3_0_6_la_LIBADD = $(libadd)
libinftextgtk3_0_6_la_HEADERS = $(headers)
libinftextgtk3_0_6_la_SOURCES = $(sources)
libinftextgtk3_0_6_ladir = \
	$(includedir)/libinftextgtk3-$(LIBINFINITY_API_VERSION)/libinftextgtk
else
libinftextgtk_0_6_la_CPPFLAGS = $(cppflags)
libinftextgtk_0_6_la_LDFLAGS = $(ldflags)
libinftextgtk_0_6_la_LIBADD = $(libadd)
libinftextgtk_0_6_la_HEADERS = $(headers)
libinftextgtk_0_6_la_SOURCES = $(sources)
libinftextgtk_0_6_ladir = \
	$(includedir)/libinftextgtk-$(LIBINFINITY_API_VERSION)/libinftextgtk
endif
