# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinfgtk-0.5.la

libinfgtk_0_5_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infgtk_CFLAGS) \
	$(infinity_CFLAGS)

libinfgtk_0_5_la_LDFLAGS = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libinfgtk_0_5_la_LIBADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(infgtk_LIBS) \
	$(infinity_LIBS)

libinfgtk_0_5_ladir = \
	$(includedir)/libinfgtk-$(LIBINFINITY_API_VERSION)/libinfgtk

libinfgtk_0_5_la_HEADERS = \
	inf-gtk-browser-model.h \
	inf-gtk-browser-model-filter.h \
	inf-gtk-browser-model-sort.h \
	inf-gtk-browser-store.h \
	inf-gtk-browser-view.h \
	inf-gtk-certificate-dialog.h \
	inf-gtk-certificate-manager.h \
	inf-gtk-certificate-view.h \
	inf-gtk-chat.h \
	inf-gtk-io.h

libinfgtk_0_5_la_SOURCES = \
	inf-gtk-browser-model.c \
	inf-gtk-browser-model-filter.c \
	inf-gtk-browser-model-sort.c \
	inf-gtk-browser-store.c \
	inf-gtk-browser-view.c \
	inf-gtk-certificate-dialog.c \
	inf-gtk-certificate-manager.c \
	inf-gtk-certificate-view.c \
	inf-gtk-chat.c \
	inf-gtk-io.c
