lib_LTLIBRARIES = libinftext-1.0.la

INCLUDES = -I${top_srcdir}

libinftext_1_0_la_CPPFLAGS = \
	$(inftext_CFLAGS)

libinftext_1_0_la_LIBADD = \
	${top_srcdir}/libinfinity/libinfinity-1.0.la \
	$(inftext_LIBS)

libinftext_1_0_ladir = $(includedir)/libinftext-1.0/libinftext

libinftext_1_0_la_HEADERS = \
	inf-text-buffer.h \
	inf-text-chunk.h \
	inf-text-default-buffer.h \
	inf-text-delete-operation.h \
	inf-text-insert-operation.h \
	inf-text-pword.h

libinftext_1_0_la_SOURCES = \
	inf-text-buffer.c \
	inf-text-chunk.c \
	inf-text-default-buffer.c \
	inf-text-delete-operation.c \
	inf-text-insert-operation.c \
	inf-text-pword.c
