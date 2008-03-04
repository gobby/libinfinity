INCLUDES = -I${top_srcdir}

noinst_LTLIBRARIES = libinfinity_server.la

libinfinity_server_la_CPPFLAGS = \
	$(infinity_CFLAGS)

libinfinity_server_ladir = $(includedir)/libinfinity-1.0/libinfinity/server

libinfinity_server_la_SOURCES = \
	infd-directory.c \
	infd-filesystem-storage.c \
	infd-server-pool.c \
	infd-session-proxy.c \
	infd-storage.c \
	infd-tcp-server.c \
	infd-xml-server.c \
	infd-xmpp-server.c

libinfinity_server_la_HEADERS = \
	infd-directory.h \
	infd-filesystem-storage.h \
	infd-note-plugin.h \
	infd-server-pool.h \
	infd-session-proxy.h \
	infd-storage.h \
	infd-tcp-server.h \
	infd-xml-server.h \
	infd-xmpp-server.h
