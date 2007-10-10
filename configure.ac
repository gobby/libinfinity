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

# Seems to be required for building a binary from multiple source files
# automake complains without it in infinoted/.
AM_PROG_CC_C_O

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
# infinoted optional package
########################

AC_ARG_WITH([infinoted], AS_HELP_STRING([--with-infinoted],
            [Build the infinote standalone server [[default=yes]]]),
            [with_infinoted=$withval], [with_infinoted=yes])

if test "x$with_infinoted" = "xyes"
then
	PKG_CHECK_MODULES([infinoted], [glib-2.0 >= 2.14 gobject-2.0 >= 2.14 gmodule-2.0 >= 2.14 gnutls])
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

		infinoted/Makefile

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
  infinoted: $with_infinoted

Enable support for:
  avahi: $use_avahi
"

# vim:set et:
