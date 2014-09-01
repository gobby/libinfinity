SUBDIRS = . libinfinity libinftext

if WITH_INFGTK
SUBDIRS += libinfgtk
endif

if WITH_INFTEXTGTK
SUBDIRS += libinftextgtk
endif

if WITH_INFINOTED
SUBDIRS += libinfinoted-plugin-manager
endif

# Note that nodist doesn't work here actually because gtk-doc pulls them
# in anyway (see content_files in a subdirectory's Makefile.am)
nodist_noinst_DATA = version.xml api_version.xml

# Remove the trailing newline of the version files, to have the entities
# in the docbook XML substituted without it.
version.xml: version.xml.in
	echo -n `cat version.xml.in` > version.xml

api_version.xml: api_version.xml.in
	echo -n `cat api_version.xml.in` > api_version.xml

CLEANFILES = version.xml api_version.xml
DISTCLEANFILES = version.xml.in api_version.xml.in
