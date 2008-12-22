m4_define([libinfinity_version], [0.2.0])

AC_PREREQ(2.59)
AC_INIT([libinfinity], [libinfinity_version], [armin@arbur.net])
AM_INIT_AUTOMAKE([-Wall dist-bzip2 check-news])
AM_MAINTAINER_MODE
AC_CONFIG_SRCDIR([libinfinity/common/inf-init.c])
AC_CONFIG_HEADER([config.h libinfinity/inf-config.h])

LIBINFINITY_VERSION=libinfinity_version
AC_SUBST(LIBINFINITY_VERSION)

AC_CANONICAL_HOST

AC_LANG(C)
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL

# Seems to be required for building a binary from multiple source files
# automake complains without it in infinoted/.
AM_PROG_CC_C_O

###################################
# Gtk-Doc
###################################

AC_CONFIG_MACRO_DIR(m4)
GTK_DOC_CHECK(1.4)

###################################
# Platform checks
###################################

case "$host_os" in
*mingw*)
  platform='win32'
  ;;
*)
  platform='unix'
  ;;
esac        

# Check for MSG_NOSIGNAL
AC_MSG_CHECKING(for MSG_NOSIGNAL)
AC_TRY_COMPILE([#include <sys/socket.h>],
	       [ int f = MSG_NOSIGNAL; ],
	       [ AC_MSG_RESULT(yes)
	         AC_DEFINE(HAVE_MSG_NOSIGNAL, 1,
			   [Define this symbol if you have MSG_NOSIGNAL]) ],
 	       [ AC_MSG_RESULT(no)]
)

###################################
# Check for regular dependencies
###################################

infinity_libraries='glib-2.0 >= 2.16 gobject-2.0 >= 2.16 gmodule-2.0 >= 2.16 libxml-2.0 gnutls >= 1.7.2 libgsasl >= 0.2.21'

# We use native Win32 functions for UUIDs, so we don't need the uuid library
# on Windows
if test $platform != 'win32'; then
  infinity_libraries="$infinity_libraries uuid"
fi

PKG_CHECK_MODULES([infinity], [$infinity_libraries])
PKG_CHECK_MODULES([inftext], [glib-2.0 >= 2.16 gobject-2.0 >= 2.16 libxml-2.0])

if test $platform = 'win32'; then
  infinity_LIBS="$infinity_LIBS -lws2_32 -lrpcrt4"
fi

###################################
# libinftextgtk optional package
###################################

AC_ARG_WITH([inftextgtk], AS_HELP_STRING([--with-inftextgtk],
            [Builds the libinftextgtk library [[default=auto]]]),
            [with_inftextgtk=$withval], [with_inftextgtk=auto])

if test "x$with_inftextgtk" = "xauto"
then
  PKG_CHECK_MODULES([inftextgtk], [gtk+-2.0 >= 2.12], [with_inftextgtk=yes], [with_inftextgtk=no])
elif test "x$with_inftextgtk" = "xyes"
then
  PKG_CHECK_MODULES([inftextgtk], [gtk+-2.0 >= 2.12])
fi

AM_CONDITIONAL([WITH_INFTEXTGTK], test "x$with_inftextgtk" = "xyes")

###############################
# libinfgtk optional package
###############################

AC_ARG_WITH([infgtk], AS_HELP_STRING([--with-infgtk],
            [Builds the libinfgtk library [[default=auto]]]),
            [with_infgtk=$withval], [with_infgtk=auto])

if test "x$with_infgtk" = "xauto"
then
  PKG_CHECK_MODULES([infgtk], [gtk+-2.0 >= 2.12], [with_infgtk=yes], [with_infgtk=no])
elif test "x$with_infgtk" = "xyes"
then
  PKG_CHECK_MODULES([infgtk], [gtk+-2.0 >= 2.12])
fi

AM_CONDITIONAL([WITH_INFGTK], test "x$with_infgtk" = "xyes")

###############################
# infinoted optional package
###############################

AC_ARG_WITH([infinoted], AS_HELP_STRING([--with-infinoted],
            [Build the infinote standalone server [[default=yes]]]),
            [with_infinoted=$withval], [with_infinoted=yes])

if test "x$with_infinoted" = "xyes"
then
  PKG_CHECK_MODULES([infinoted], [glib-2.0 >= 2.16 gobject-2.0 >= 2.16 gmodule-2.0 >= 2.14 gnutls >= 1.7.2])
fi

AM_CONDITIONAL([WITH_INFINOTED], test "x$with_infinoted" = "xyes")

####################
# Check for avahi
####################

AC_ARG_WITH([avahi], AS_HELP_STRING([--with-avahi],
            [Enables avahi support [[default=auto]]]),
            [use_avahi=$withval], [use_avahi=auto])

if test "x$use_avahi" = "xauto"
then
  PKG_CHECK_MODULES([avahi], [avahi-client], [use_avahi=yes], [use_avahi=no])
elif test "x$use_avahi" = "xyes"
then
  PKG_CHECK_MODULES([avahi], [avahi-client])
fi

if test "x$use_avahi" = "xyes"
then
  AC_DEFINE([LIBINFINITY_HAVE_AVAHI], 1, [Whether avahi support is enabled])
fi

AM_CONDITIONAL([LIBINFINITY_HAVE_AVAHI], test "x$use_avahi" = "xyes")

###################
# Traffic output
###################

AC_ARG_ENABLE([traffic_output], AS_HELP_STRING([--enable-traffic-output],
              [Shows XMPP traffic on the terminal [[default=no]]]),
              [enable_traffic_output=$enableval], [enable_traffic_output=no])

if test "x$enable_traffic_output" = "xyes"; then
  AC_DEFINE([INF_XMPP_CONNECTION_PRINT_TRAFFIC], 1,
            [Whether to print XMPP traffic to the terminal])
fi

############
# gettext
############

GETTEXT_PACKAGE=libinfinity10
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"],
                   [The domain name to use with gettext.])

ALL_LINGUAS="`grep -v '^#' "$srcdir/po/LINGUAS" | tr '\n' ' '`"
AM_GLIB_GNU_GETTEXT
AM_GLIB_DEFINE_LOCALEDIR(INF_LOCALEDIR)

###########
# Output
###########

AC_CONFIG_FILES([
  Makefile
    libinfinity/Makefile
      libinfinity/communication/Makefile
      libinfinity/common/Makefile
      libinfinity/adopted/Makefile
      libinfinity/server/Makefile
      libinfinity/client/Makefile

    libinftext/Makefile
    libinfgtk/Makefile
    libinftextgtk/Makefile

    infinoted/Makefile
      infinoted/note-plugins/Makefile
        infinoted/note-plugins/text/Makefile

    pixmaps/Makefile
      pixmaps/16x16/Makefile
      pixmaps/22x22/Makefile
      pixmaps/scalable/Makefile

    test/Makefile
      test/util/Makefile
      test/session/Makefile
      test/cleanup/Makefile

    docs/Makefile
      docs/reference/version.xml
      docs/reference/Makefile
        docs/reference/libinfinity/Makefile
        docs/reference/libinftext/Makefile
        docs/reference/libinfgtk/Makefile
        docs/reference/libinftextgtk/Makefile

    po/Makefile.in

  libinfinity-1.0.pc
  libinftext-1.0.pc
  libinfgtk-1.0.pc
  libinftextgtk-1.0.pc
])

AC_OUTPUT

echo "
Build optional packages:
  libinfgtk: $with_infgtk
  libinftextgtk: $with_inftextgtk
  infinoted: $with_infinoted

Enable support for:
  avahi: $use_avahi
"

# vim:set et:
