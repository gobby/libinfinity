noinst_LTLIBRARIES = libinfinity_client.la

libinfinity_client_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinity_CFLAGS)

libinfinity_client_ladir = \
	$(includedir)/libinfinity-$(LIBINFINITY_API_VERSION)/libinfinity/client

libinfinity_client_la_SOURCES = \
	infc-browser.c \
	infc-browser-iter.c \
	infc-explore-request.c \
	infc-node-request.c \
	infc-request.c \
	infc-request-manager.c \
	infc-session-proxy.c \
	infc-user-request.c

libinfinity_client_la_HEADERS = \
	infc-browser.h \
	infc-browser-iter.h \
	infc-explore-request.h \
	infc-node-request.h \
	infc-note-plugin.h \
	infc-request.h \
	infc-request-manager.h \
	infc-session-proxy.h \
	infc-user-request.h
