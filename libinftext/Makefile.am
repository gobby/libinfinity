lib_LTLIBRARIES = libinftext-1.0.la

AM_CPPFLAGS = -I${top_srcdir}

libinftext_1_0_la_CPPFLAGS = \
	$(inftext_CFLAGS) \
	$(infinity_CFLAGS)

libinftext_1_0_la_LDFLAGS = \
	-no-undefined

libinftext_1_0_la_LIBADD = \
	${top_srcdir}/libinfinity/libinfinity-1.0.la \
	$(inftext_LIBS) \
	$(infinity_LIBS)

libinftext_1_0_ladir = $(includedir)/libinftext-1.0/libinftext

libinftext_1_0_la_HEADERS = \
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
	inf-text-user.h

libinftext_1_0_la_SOURCES = \
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
	inf-text-user.c
