lib_LTLIBRARIES = libinfgtk-1.0.la

libinfgtk_1_0_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infgtk_CFLAGS) \
	$(infinity_CFLAGS)

libinfgtk_1_0_la_LDFLAGS = \
	-no-undefined

libinfgtk_1_0_la_LIBADD = \
	${top_builddir}/libinfinity/libinfinity-1.0.la \
	$(infgtk_LIBS) \
	$(infinity_LIBS)

libinfgtk_1_0_ladir = $(includedir)/libinfgtk-1.0/libinfgtk

libinfgtk_1_0_la_HEADERS = \
	inf-gtk-browser-model.h \
	inf-gtk-browser-model-filter.h \
	inf-gtk-browser-model-sort.h \
	inf-gtk-browser-store.h \
	inf-gtk-browser-view.h \
	inf-gtk-certificate-dialog.h \
	inf-gtk-certificate-manager.h \
	inf-gtk-certificate-view.h \
	inf-gtk-io.h

libinfgtk_1_0_la_SOURCES = \
	inf-gtk-browser-model.c \
	inf-gtk-browser-model-filter.c \
	inf-gtk-browser-model-sort.c \
	inf-gtk-browser-store.c \
	inf-gtk-browser-view.c \
	inf-gtk-certificate-dialog.c \
	inf-gtk-certificate-manager.c \
	inf-gtk-certificate-view.c \
	inf-gtk-io.c
