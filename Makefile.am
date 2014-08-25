SUBDIRS = libinfinity libinftext

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
	libinfinity-$(LIBINFINITY_API_VERSION).pc \
	libinftext-$(LIBINFINITY_API_VERSION).pc

if WITH_INFGTK
SUBDIRS += libinfgtk
pkgconfig_DATA += libinf$(infgtkver)-$(LIBINFINITY_API_VERSION).pc
endif

if WITH_INFTEXTGTK
SUBDIRS += libinftextgtk
pkgconfig_DATA += libinftext$(infgtkver)-$(LIBINFINITY_API_VERSION).pc
endif

if WITH_INFINOTED
SUBDIRS += infinoted
pkgconfig_DATA += libinfinoted-plugin-manager-$(LIBINFINITY_API_VERSION).pc
endif

SUBDIRS += pixmaps test docs po

EXTRA_DIST = README.md

DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

MAINTAINERCLEANFILES = \
	ChangeLog

distclean-local:
	if test "$(srcdir)" = "."; then :; else \
		rm -f ChangeLog; \
	fi

ChangeLog:
	$(AM_V_GEN) if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log \
	        4021c6e493112f901fb5bfb5daf4d3546272ac69..HEAD --stat=78) \
	        | fmt --split-only --width=78 > $@.tmp \
	  && cat $@.tmp ChangeLog.manual > $@ && $(RM) $@.tmp \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

.PHONY: ChangeLog
