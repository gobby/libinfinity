libinfinity_communication_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinity_CFLAGS) \
	$(avahi_CFLAGS)

noinst_LTLIBRARIES = libinfinity_communication.la

libinfinity_communication_ladir = \
	$(includedir)/libinfinity-$(LIBINFINITY_API_VERSION)/libinfinity/communication

libinfinity_communication_la_SOURCES = \
	inf-communication-central-factory.c \
	inf-communication-central-method.c \
	inf-communication-factory.c \
	inf-communication-group.c \
	inf-communication-hosted-group.c \
	inf-communication-joined-group.c \
	inf-communication-manager.c \
	inf-communication-method.c \
	inf-communication-object.c \
	inf-communication-registry.c

libinfinity_communication_la_HEADERS = \
	inf-communication-central-factory.h \
	inf-communication-central-method.h \
	inf-communication-factory.h \
	inf-communication-group.h \
	inf-communication-hosted-group.h \
	inf-communication-joined-group.h \
	inf-communication-manager.h \
	inf-communication-method.h \
	inf-communication-object.h \
	inf-communication-registry.h

noinst_HEADERS = \
	inf-communication-group-private.h
