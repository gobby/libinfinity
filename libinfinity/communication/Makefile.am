libinfinity_communication_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinity_CFLAGS) \
	$(avahi_CFLAGS)

noinst_LTLIBRARIES = libinfinity_communication.la

libinfinity_communication_ladir = \
	$(includedir)/libinfinity-1.0/libinfinity/communication

libinfinity_communication_la_SOURCES = \
	inf-communication-factory.c \
	inf-communication-group.c \
	inf-communication-hosted-group.c \
	inf-communication-joined-group.c \
	inf-communication-manager.c \
	inf-communication-method.c \
	inf-communication-object.c

libinfinity_communication_la_HEADERS = \
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
