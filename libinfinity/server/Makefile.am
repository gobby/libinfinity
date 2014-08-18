noinst_LTLIBRARIES = libinfinity_server.la

libinfinity_server_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinity_CFLAGS)

libinfinity_server_ladir = \
	$(includedir)/libinfinity-$(LIBINFINITY_API_VERSION)/libinfinity/server

libinfinity_server_la_SOURCES = \
	infd-account-storage.c \
	infd-chat-filesystem-format.c \
	infd-directory.c \
	infd-filesystem-account-storage.c \
	infd-filesystem-storage.c \
	infd-progress-request.c \
	infd-request.c \
	infd-server-pool.c \
	infd-session-proxy.c \
	infd-storage.c \
	infd-tcp-server.c \
	infd-xml-server.c \
	infd-xmpp-server.c

libinfinity_server_la_HEADERS = \
	infd-account-storage.h \
	infd-chat-filesystem-format.h \
	infd-directory.h \
	infd-filesystem-account-storage.h \
	infd-filesystem-storage.h \
	infd-note-plugin.h \
	infd-progress-request.h \
	infd-request.h \
	infd-server-pool.h \
	infd-session-proxy.h \
	infd-storage.h \
	infd-tcp-server.h \
	infd-xml-server.h \
	infd-xmpp-server.h
