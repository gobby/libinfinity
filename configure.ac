AC_PREREQ(2.59)
AC_INIT([infinote], [0.1.0], [armin@arbur.net])
AM_INIT_AUTOMAKE([infinote], 1.9, check-news)
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

PKG_CHECK_MODULES([infinity], [glib-2.0 >= 2.12 gobject-2.0 >= 2.12 libxml-2.0 gnutls libgsasl])

########################
# Compile time options
########################

# TODO: Should this be --enable instead of --with?
AC_ARG_WITH([server], AC_HELP_STRING([--with-server], [Enables the infinote server [default=yes])]), use_server=$withval, use_server=yes)

if test $use_server = yes
then
	AC_DEFINE([WITH_SERVER], 1, [Build the infinote server])
fi

####################
# Check for avahi
####################

AC_ARG_WITH([avahi], AC_HELP_STRING([--with-avahi], [Enables avahi support [default=auto])]), use_avahi=$withval, use_avahi=auto)

if test $use_avahi = auto 
then
	PKG_CHECK_MODULES([avahi], [avahi-client avahi-glib], use_avahi=yes, use_avahi=no)
elif test $use_avahi = yes
then
	PKG_CHECK_MODULES([avahi], [avahi-client avahi-glib])
fi

if test $use_avahi = yes
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
		libinfinity/server/Makefile

		test/Makefile
])

AC_OUTPUT
