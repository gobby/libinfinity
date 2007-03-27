AC_PREREQ(2.59)
AC_INIT([infinote], [0.1.0], [armin@arbur.net])
AM_INIT_AUTOMAKE([infinote], 1.9, check-news)
AM_MAINTAINER_MODE
AC_CONFIG_SRCDIR([libinfinity/inf-net-object.h])
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

PKG_CHECK_MODULES([glib], [glib-2.0 >= 2.12])
PKG_CHECK_MODULES([gobject], [gobject-2.0 >= 2.12])
PKG_CHECK_MODULES([libxml2], [libxml-2.0])
PKG_CHECK_MODULES([gnutls], [gnutls])
PKG_CHECK_MODULES([gsasl], [libgsasl])

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
		libinfinited/Makefile

		test/Makefile
])

AC_OUTPUT
