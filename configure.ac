AC_PREREQ(2.59)
AC_INIT([infinote], [0.1.0], [armin@arbur.net])
AM_INIT_AUTOMAKE([1.9 check-news])
AM_MAINTAINER_MODE
AC_CONFIG_SRCDIR([libinfinity/common/inf-net-object.h])
AC_CONFIG_HEADER([libinfinity/inf-config.h])

AC_CANONICAL_HOST

AC_LANG(C)
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_LIBTOOL

###################################
# Check for regular dependencies
###################################

PKG_CHECK_MODULES([infinity], [glib-2.0 >= 2.14 gobject-2.0 >= 2.14 libxml-2.0 gnutls libgsasl])
PKG_CHECK_MODULES([inftext], [glib-2.0 >= 2.14 gobject-2.0 >= 2.14 libxml-2.0])

########################
# libinfgtk optional package
########################

AC_ARG_WITH([infgtk], AS_HELP_STRING([--with-infgtk],
            [Builds the libinfgtk library [[default=auto]]]),
            [with_infgtk=$withval], [with_infgtk=auto])

if test "x$with_infgtk" = "xauto"
then
	PKG_CHECK_MODULES([infgtk], [gtk+-2.0 >= 2.10], [with_infgtk=yes], [with_infgtk=no])
elif test "x$with_infgtk" = "xyes"
then
	PKG_CHECK_MODULES([infgtk], [gtk+-2.0 >= 2.10])
fi

AM_CONDITIONAL([WITH_INFGTK], test "x$with_infgtk" = "xyes")

########################
# Compile time options
########################

AC_ARG_ENABLE([server],
              AS_HELP_STRING([--with-server],
                             [Enables the infinote server [[default=yes]]]),
              [use_server=$withval], [use_server=yes])

if test "x$use_server" = "xyes"
then
	AC_DEFINE([WITH_SERVER], 1, [Build the infinote server])
fi

####################
# Check for avahi
####################

AC_ARG_WITH([avahi], AS_HELP_STRING([--with-avahi],
            [Enables avahi support [[default=auto]]]),
            [use_avahi=$withval], [use_avahi=auto])

if test "x$use_avahi" = "xauto"
then
	PKG_CHECK_MODULES([avahi], [avahi-client avahi-glib],
                          [use_avahi=yes], [use_avahi=no])
elif test "x$use_avahi" = "xyes"
then
	PKG_CHECK_MODULES([avahi], [avahi-client avahi-glib])
fi

if test "x$use_avahi" = "xyes"
then
	AC_DEFINE([HAVE_AVAHI], 1, [Have Avahi])
fi

###########
# Output
###########

AC_CONFIG_FILES([
	Makefile
		libinfinity/Makefile
			libinfinity/common/Makefile
			libinfinity/adopted/Makefile
			libinfinity/server/Makefile
			libinfinity/client/Makefile

		libinftext/Makefile
		libinfgtk/Makefile

		pixmaps/Makefile
			pixmaps/16x16/Makefile
			pixmaps/22x22/Makefile
			pixmaps/scalable/Makefile

		test/Makefile
	libinfinity-1.0.pc
	libinftext-1.0.pc
	libinfgtk-1.0.pc
])

AC_OUTPUT

echo "
Build optional packages:
  libinfgtk: $with_infgtk

Enable support for:
  avahi: $use_avahi
"

# vim:set et:

