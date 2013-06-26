m4_define([libinfinity_version], [0.6.0])
m4_define([libinfinity_api_version], [0.6])
m4_define([libinfinity_libtool_version], [0:0:0])

AC_PREREQ(2.60)
AC_INIT([libinfinity], [libinfinity_version], [armin@arbur.net])
AM_INIT_AUTOMAKE([-Wall dist-bzip2 check-news])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES])
AM_MAINTAINER_MODE
AC_CONFIG_SRCDIR([libinfinity/common/inf-init.c])
AC_CONFIG_HEADER([config.h libinfinity/inf-config.h])

LIBINFINITY_VERSION=libinfinity_version
LIBINFINITY_API_VERSION=libinfinity_api_version
LIBINFINITY_LIBTOOL_VERSION=libinfinity_libtool_version
AC_SUBST(LIBINFINITY_VERSION)
AC_SUBST(LIBINFINITY_API_VERSION)
AC_SUBST(LIBINFINITY_LIBTOOL_VERSION)

AC_CANONICAL_HOST

# Remove -Wno-strict-prototypes for the feature tests
has_strict_prototypes=""
if (echo $CFLAGS | grep -- -Wstrict-prototype)
then
        has_strict_prototypes="true"
        old_CFLAGS=$CFLAGS
        CFLAGS="$CFLAGS -Wno-strict-prototypes"
fi

# Make GNU and POSIX extensions such as readdir_r, O_NOFOLLOW and
# fdopendir available
AC_USE_SYSTEM_EXTENSIONS

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

# Check for SO_REUSEADDR
AC_MSG_CHECKING(for SO_REUSEADDR)
AC_TRY_COMPILE([#include <sys/socket.h>
                #include <stdio.h> ],
	       [ int f = SO_REUSEADDR; printf("%d\n", f); ],
	       [ AC_MSG_RESULT(yes)
	         AC_DEFINE(HAVE_SO_REUSEADDR, 1,
			   [Define this symbol if you have SO_REUSEADDR]) ],
	       [ AC_MSG_RESULT(no)]
)

# Check for dirent.d_type
AC_MSG_CHECKING(for d_type)
AC_TRY_COMPILE([#include <dirent.h>
                #include <stdio.h> ],
               [ struct dirent d; printf("%d\n", d.d_type); ],
               [ AC_MSG_RESULT(yes)
                 AC_DEFINE(HAVE_D_TYPE, 1,
                           [Define this symbol if your struct dirent has the 
                            d_type field])],
               [ AC_MSG_RESULT(no)]
)

###################################
# Check for regular dependencies
###################################

infinity_libraries='glib-2.0 >= 2.16 gobject-2.0 >= 2.16 gmodule-2.0 >= 2.16 gthread-2.0 >= 2.16 libxml-2.0 gnutls >= 2.12.0 libgsasl >= 0.2.21'

PKG_CHECK_MODULES([infinity], [$infinity_libraries])
PKG_CHECK_MODULES([inftext], [glib-2.0 >= 2.16 gobject-2.0 >= 2.16 libxml-2.0])

if test $platform = 'win32'; then
  infinity_LIBS="$infinity_LIBS -lws2_32"
fi

###################################
# Check for GTK+ 3
###################################

AC_ARG_WITH([gtk3], AS_HELP_STRING([--with-gtk3],
            [Builds libinfgtk and libinftextgtk against GTK+ 3 [[default=no]]]),
	    [with_gtk3=$withval], [with_gtk3=no])

if test "x$with_gtk3" = "xyes"
then
	gtk_requirement="gtk+-3.0 >= 2.90.6"
	gtk_version=3
	infgtkver=gtk3
else
	gtk_requirement="gtk+-2.0 >= 2.12"
	gtk_version=2
	infgtkver=gtk
fi

AM_CONDITIONAL([WITH_GTK3], test "x$with_gtk3" = "xyes")

AC_SUBST(gtk_requirement)
AC_SUBST(gtk_version)
AC_SUBST(infgtkver)

###################################
# libinftextgtk optional package
###################################

AC_ARG_WITH([inftextgtk], AS_HELP_STRING([--with-inftextgtk],
            [Builds the libinftextgtk library [[default=auto]]]),
            [with_inftextgtk=$withval], [with_inftextgtk=auto])

if test "x$with_inftextgtk" = "xauto"
then
  PKG_CHECK_MODULES([inftextgtk], [$gtk_requirement], [with_inftextgtk=yes], [with_inftextgtk=no])
elif test "x$with_inftextgtk" = "xyes"
then
  PKG_CHECK_MODULES([inftextgtk], [$gtk_requirement])
fi

if test "x$with_inftextgtk" = "xyes"
then
  AC_CHECK_LIB([m], [atan2], [inftextgtk_LIBS="$inftextgtk_LIBS -lm"], AC_MSG_ERROR(libm not found))
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
  PKG_CHECK_MODULES([infgtk], [$gtk_requirement], [with_infgtk=yes], [with_infgtk=no])
elif test "x$with_infgtk" = "xyes"
then
  PKG_CHECK_MODULES([infgtk], [$gtk_requirement])
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

####################
# Check for libdaemon
####################

AC_ARG_WITH([libdaemon], AS_HELP_STRING([--with-libdaemon],
            [Enables daemonizing support in infinoted [[default=auto]]]),
            [use_libdaemon=$withval], [use_libdaemon=auto])

if test "x$use_libdaemon" = "xauto"
then
  PKG_CHECK_MODULES([libdaemon], [libdaemon], [use_libdaemon=yes], [use_libdaemon=no])
elif test "x$use_libdaemon" = "xyes"
then
  PKG_CHECK_MODULES([libdaemon], [libdaemon])
fi

if test "x$use_libdaemon" = "xyes"
then
  AC_DEFINE([LIBINFINITY_HAVE_LIBDAEMON], 1, [Whether libdaemon support is enabled])
fi

AM_CONDITIONAL([LIBINFINITY_HAVE_LIBDAEMON], test "x$use_libdaemon" = "xyes")

#################
# Check for pam #
#################

AC_MSG_CHECKING(for pam)
AC_CHECK_LIB([ pam ],
             [ pam_start ],
             [ AC_MSG_RESULT(yes)
               AC_DEFINE(LIBINFINITY_HAVE_PAM, 1,
                         [Define this symbol if you have pam])
               use_pam=yes
               infinity_LIBS="$infinity_LIBS -lpam" ],
             [ AC_MSG_RESULT(no)
               use_pam=no ]
)

# Check for PAM_FAIL_DELAY
AC_MSG_CHECKING(for PAM_FAIL_DELAY)
AC_TRY_COMPILE([#include <security/pam_appl.h>
                #include <stdio.h> ],
               [ int f = PAM_FAIL_DELAY; printf("%d\n", f); ],
               [ AC_MSG_RESULT(yes)
                 AC_DEFINE(HAVE_PAM_FAIL_DELAY, 1,
                           [Define this symbol if your pam
                            has PAM_FAIL_DELAY])],
               [ AC_MSG_RESULT(no)]
)

############
# gettext
############

GETTEXT_PACKAGE="libinfinity-libinfinity_api_version"
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"],
                   [The domain name to use with gettext.])

ALL_LINGUAS="`grep -v '^#' "$srcdir/po/LINGUAS" | tr '\n' ' '`"
AM_GLIB_GNU_GETTEXT
AM_GLIB_DEFINE_LOCALEDIR(INF_LOCALEDIR)

###########
# Output
###########

if test -n $has_strict_prototypes
then
        CFLAGS=$old_CFLAGS
fi

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
      docs/reference/Makefile
      docs/reference/version.xml.in
      docs/reference/api_version.xml.in
        docs/reference/libinfinity/Makefile
        docs/reference/libinftext/Makefile
        docs/reference/libinfgtk/Makefile
        docs/reference/libinftextgtk/Makefile

    po/Makefile.in
])

# These are separate for the macros to be expanded
# (note no square brackets)
AC_CONFIG_FILES(
  libinfinity-libinfinity_api_version.pc:libinfinity.pc.in
  libinftext-libinfinity_api_version.pc:libinftext.pc.in
  libinf${infgtkver}-libinfinity_api_version.pc:libinfgtk.pc.in
  libinftext${infgtkver}-libinfinity_api_version.pc:libinftextgtk.pc.in
)

AC_OUTPUT

echo "
Build optional packages:
  libinfgtk: $with_infgtk
  libinftextgtk: $with_inftextgtk
  infinoted: $with_infinoted

Enable support for:
  gtk: version $gtk_version
  avahi: $use_avahi
  libdaemon: $use_libdaemon
  pam: $use_pam
"

# vim:set et:
