lib_LTLIBRARIES = libinfgtk-1.0.la

INCLUDES = -I${top_srcdir}

libinfgtk_1_0_la_CPPFLAGS = \
	$(infinity_CFLAGS) \
	$(infgtk_CFLAGS)

libinfgtk_1_0_la_LIBADD = \
	${top_srcdir}/libinfinity/libinfinity-1.0.la \
	$(infinity_LIBS) \
	$(infgtk_LIBS)

libinfgtk_1_0_ladir = $(includedir)/libinfgtk-1.0/libinfgtk

libinfgtk_1_0_la_HEADERS = \
	inf-gtk-browser-model.h \
	inf-gtk-browser-model-filter.h \
	inf-gtk-browser-model-sort.h \
	inf-gtk-browser-store.h \
	inf-gtk-browser-view.h \
	inf-gtk-io.h

libinfgtk_1_0_la_SOURCES = \
	inf-gtk-browser-model.c \
	inf-gtk-browser-model-filter.c \
	inf-gtk-browser-model-sort.c \
	inf-gtk-browser-store.c \
	inf-gtk-browser-view.c \
	inf-gtk-io.c
