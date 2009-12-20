# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinftext-0.5.la

libinftext_0_5_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(inftext_CFLAGS) \
	$(infinity_CFLAGS)

libinftext_0_5_la_LDFLAGS = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libinftext_0_5_la_LIBADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinftext_0_5_ladir = \
	$(includedir)/libinftext-$(LIBINFINITY_API_VERSION)/libinftext

libinftext_0_5_la_HEADERS = \
	inf-text-buffer.h \
	inf-text-chunk.h \
	inf-text-default-buffer.h \
	inf-text-default-delete-operation.h \
	inf-text-default-insert-operation.h \
	inf-text-delete-operation.h \
	inf-text-insert-operation.h \
	inf-text-move-operation.h \
	inf-text-operations.h \
	inf-text-remote-delete-operation.h \
	inf-text-session.h \
	inf-text-undo-grouping.h \
	inf-text-user.h

libinftext_0_5_la_SOURCES = \
	inf-text-buffer.c \
	inf-text-chunk.c \
	inf-text-default-buffer.c \
	inf-text-default-delete-operation.c \
	inf-text-default-insert-operation.c \
	inf-text-delete-operation.c \
	inf-text-insert-operation.c \
	inf-text-move-operation.c \
	inf-text-remote-delete-operation.c \
	inf-text-session.c \
	inf-text-undo-grouping.c \
	inf-text-user.c
