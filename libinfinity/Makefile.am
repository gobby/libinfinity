# TODO: Find a way to have the version number set automatically.
lib_LTLIBRARIES = libinfinity-0.7.la

libinfinity_0_7_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(infinity_CFLAGS) \
	$(avahi_CFLAGS)

libinfinity_0_7_la_LDFLAGS = \
	-no-undefined \
	-version-info $(LIBINFINITY_LIBTOOL_VERSION)

libinfinity_0_7_la_LIBADD = \
	$(infinity_LIBS) \
	$(glib_LIBS) \
	$(avahi_LIBS)

libinfinity_0_7_ladir = \
	$(includedir)/libinfinity-$(LIBINFINITY_API_VERSION)/libinfinity

adopteddir = $(libinfinity_0_7_ladir)/adopted
adopted_HEADERS = \
	adopted/inf-adopted-algorithm.h \
	adopted/inf-adopted-no-operation.h \
	adopted/inf-adopted-operation.h \
	adopted/inf-adopted-request.h \
	adopted/inf-adopted-request-log.h \
	adopted/inf-adopted-session.h \
	adopted/inf-adopted-session-record.h \
	adopted/inf-adopted-session-replay.h \
	adopted/inf-adopted-split-operation.h \
	adopted/inf-adopted-state-vector.h \
	adopted/inf-adopted-undo-grouping.h \
	adopted/inf-adopted-user.h

commondir = $(libinfinity_0_7_ladir)/common
common_HEADERS = \
	common/inf-acl.h \
	common/inf-async-operation.h \
	common/inf-browser.h \
	common/inf-browser-iter.h \
	common/inf-buffer.h \
	common/inf-certificate-chain.h \
	common/inf-certificate-credentials.h \
	common/inf-certificate-verify.h \
	common/inf-cert-util.h \
	common/inf-chat-buffer.h \
	common/inf-chat-session.h \
	common/inf-discovery.h \
	common/inf-discovery-avahi.h \
	common/inf-error.h \
	common/inf-file-util.h \
	common/inf-init.h \
	common/inf-io.h \
	common/inf-ip-address.h \
	common/inf-keepalive.h \
	common/inf-local-publisher.h \
	common/inf-name-resolver.h \
	common/inf-native-socket.h \
	common/inf-protocol.h \
	common/inf-request.h \
	common/inf-request-result.h \
	common/inf-sasl-context.h \
	common/inf-session.h \
	common/inf-session-proxy.h \
	common/inf-simulated-connection.h \
	common/inf-standalone-io.h \
	common/inf-tcp-connection.h \
	common/inf-user.h \
	common/inf-user-table.h \
	common/inf-xml-connection.h \
	common/inf-xml-util.h \
	common/inf-xmpp-connection.h \
	common/inf-xmpp-manager.h

communicationdir = $(libinfinity_0_7_ladir)/communication
communication_HEADERS = \
	communication/inf-communication-central-factory.h \
	communication/inf-communication-central-method.h \
	communication/inf-communication-factory.h \
	communication/inf-communication-group.h \
	communication/inf-communication-hosted-group.h \
	communication/inf-communication-joined-group.h \
	communication/inf-communication-manager.h \
	communication/inf-communication-method.h \
	communication/inf-communication-object.h \
	communication/inf-communication-registry.h

clientdir = $(libinfinity_0_7_ladir)/client
client_HEADERS = \
	client/infc-browser.h \
	client/infc-note-plugin.h \
	client/infc-progress-request.h \
	client/infc-request.h \
	client/infc-request-manager.h \
	client/infc-session-proxy.h

serverdir = $(libinfinity_0_7_ladir)/server
server_HEADERS = \
	server/infd-account-storage.h \
	server/infd-chat-filesystem-format.h \
	server/infd-directory.h \
	server/infd-filesystem-account-storage.h \
	server/infd-filesystem-storage.h \
	server/infd-note-plugin.h \
	server/infd-progress-request.h \
	server/infd-request.h \
	server/infd-server-pool.h \
	server/infd-session-proxy.h \
	server/infd-storage.h \
	server/infd-tcp-server.h \
	server/infd-xml-server.h \
	server/infd-xmpp-server.h

libinfinity_0_7_la_HEADERS = \
	inf-config.h

noinst_HEADERS = \
	common/inf-tcp-connection-private.h \
	communication/inf-communication-group-private.h \
	inf-define-enum.h \
	inf-dll.h \
	inf-i18n.h \
	inf-signals.h

commonSOURCES = \
	adopted/inf-adopted-algorithm.c \
	adopted/inf-adopted-no-operation.c \
	adopted/inf-adopted-operation.c \
	adopted/inf-adopted-request.c \
	adopted/inf-adopted-request-log.c \
	adopted/inf-adopted-session.c \
	adopted/inf-adopted-session-record.c \
	adopted/inf-adopted-session-replay.c \
	adopted/inf-adopted-split-operation.c \
	adopted/inf-adopted-state-vector.c \
	adopted/inf-adopted-undo-grouping.c \
	adopted/inf-adopted-user.c \
	common/inf-acl.c \
	common/inf-async-operation.c \
	common/inf-browser.c \
	common/inf-browser-iter.c \
	common/inf-buffer.c \
	common/inf-certificate-chain.c \
	common/inf-certificate-credentials.c \
	common/inf-certificate-verify.c \
	common/inf-cert-util.c \
	common/inf-chat-buffer.c \
	common/inf-chat-session.c \
	common/inf-discovery-avahi.c \
	common/inf-discovery.c \
	common/inf-error.c \
	common/inf-file-util.c \
	common/inf-init.c \
	common/inf-io.c \
	common/inf-ip-address.c \
	common/inf-keepalive.c \
	common/inf-local-publisher.c \
	common/inf-name-resolver.c \
	common/inf-native-socket.c \
	common/inf-protocol.c \
	common/inf-request.c \
	common/inf-request-result.c \
	common/inf-sasl-context.c \
	common/inf-session.c \
	common/inf-session-proxy.c \
	common/inf-simulated-connection.c \
	common/inf-standalone-io.c \
	common/inf-tcp-connection.c \
	common/inf-user.c \
	common/inf-user-table.c \
	common/inf-xml-connection.c \
	common/inf-xml-util.c \
	common/inf-xmpp-connection.c \
	common/inf-xmpp-manager.c \
	communication/inf-communication-central-factory.c \
	communication/inf-communication-central-method.c \
	communication/inf-communication-factory.c \
	communication/inf-communication-group.c \
	communication/inf-communication-hosted-group.c \
	communication/inf-communication-joined-group.c \
	communication/inf-communication-manager.c \
	communication/inf-communication-method.c \
	communication/inf-communication-object.c \
	communication/inf-communication-registry.c

clientSOURCES = \
	client/infc-browser.c \
	client/infc-progress-request.c \
	client/infc-request.c \
	client/infc-request-manager.c \
	client/infc-session-proxy.c

serverSOURCES = \
	server/infd-account-storage.c \
	server/infd-chat-filesystem-format.c \
	server/infd-directory.c \
	server/infd-filesystem-account-storage.c \
	server/infd-filesystem-storage.c \
	server/infd-progress-request.c \
	server/infd-request.c \
	server/infd-server-pool.c \
	server/infd-session-proxy.c \
	server/infd-storage.c \
	server/infd-tcp-server.c \
	server/infd-xml-server.c \
	server/infd-xmpp-server.c

libinfinity_0_7_la_SOURCES = \
	${commonSOURCES} \
	${clientSOURCES} \
	${serverSOURCES} \
	inf-dll.c \
	inf-i18n.c \
	inf-signals.c

if HAVE_INTROSPECTION
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS = Infinity-0.7.gir Infinityc-0.7.gir Infinityd-0.7.gir
INTROSPECTION_SCANNER_ENV = CC="${CC}"

Infinity-0.7.gir: libinfinity-0.7.la
Infinityc-0.7.gir: libinfinity-0.7.la Infinity-0.7.gir
Infinityd-0.7.gir: libinfinity-0.7.la Infinity-0.7.gir

Infinity_0_7_gir_SCANNERFLAGS = \
	-I${top_srcdir} \
	-n Infinity \
	--add-include-path=${top_srcdir}/libinfinity \
	--identifier-prefix=Inf

Infinity_0_7_gir_NAMESPACE = Infinity
Infinity_0_7_gir_VERSION = 0.7
Infinity_0_7_gir_CFLAGS = $(infinity_CFLAGS)
Infinity_0_7_gir_LIBS = libinfinity-0.7.la
Infinity_0_7_gir_FILES = \
	$(commonSOURCES) \
	$(adopted_HEADERS) \
	$(common_HEADERS) \
	$(communication_HEADERS)

# We ship simple "stub" .gir files for gnutls and gsasl, since we expose some
# of these objects and expect the user of the library to use their API.
Infinity_0_7_gir_INCLUDES = GObject-2.0 libxml2-2.0 InfGnutls-3.0 InfGsasl-1.0
Infinity_0_7_gir_PACKAGES = gobject-2.0
Infinity_0_7_gir_EXPORT_PACKAGES = libinfinity-0.7

# TODO: I would prefer to have all symbols in one gir, however that does not
# work at the moment since the prefix for client or server specific functions
# have a different prefix. We should just rename those classes, so that all
# prefixes inside libinfinity are the same.
Infinityc_0_7_gir_SCANNERFLAGS = \
	-I${top_srcdir} \
	--add-include-path=${top_srcdir}/libinfinity \
	-n Infinityc \
	--identifier-prefix=Infc

Infinityc_0_7_gir_NAMESPACE = Infinityc
Infinityc_0_7_gir_VERSION = 0.7
Infinityc_0_7_gir_CFLAGS = $(infinity_CFLAGS)
Infinityc_0_7_gir_LIBS = libinfinity-0.7.la
Infinityc_0_7_gir_FILES = \
	$(clientSOURCES) \
	$(client_HEADERS)

Infinityc_0_7_gir_INCLUDES = Infinity-0.7
Infinityc_0_7_gir_PACKAGES = gobject-2.0
Infinityc_0_7_gir_EXPORT_PACKAGES = libinfinity-0.7

Infinityd_0_7_gir_SCANNERFLAGS = \
	-I${top_srcdir} \
	--add-include-path=${top_srcdir}/libinfinity \
	-n Infinityd \
	--identifier-prefix=Infd

Infinityd_0_7_gir_NAMESPACE = Infinityd
Infinityd_0_7_gir_VERSION = 0.7
Infinityd_0_7_gir_CFLAGS = $(infinity_CFLAGS)
Infinityd_0_7_gir_LIBS = libinfinity-0.7.la
Infinityd_0_7_gir_FILES = \
	$(serverSOURCES) \
	$(server_HEADERS)

Infinityd_0_7_gir_INCLUDES = Infinity-0.7
Infinityd_0_7_gir_PACKAGES = gobject-2.0
Infinityd_0_7_gir_EXPORT_PACKAGES = libinfinity-0.7

girdir = $(datadir)/gir-1.0
gir_DATA = InfGnutls-3.0.gir InfGsasl-1.0.gir Infinity-0.7.gir Infinityc-0.7.gir Infinityd-0.7.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = InfGnutls-3.0.typelib InfGsasl-1.0.typelib Infinity-0.7.typelib Infinityc-0.7.typelib Infinityd-0.7.typelib

CLEANFILES = \
	$(INTROSPECTION_GIRS) \
	$(typelib_DATA)
endif
