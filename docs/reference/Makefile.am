SUBDIRS = . libinfinity libinftext libinfgtk libinftextgtk libinfinoted-plugin-manager

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
