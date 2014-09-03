# libinfinity

[![Build Status](https://travis-ci.org/gobby/libinfinity.svg?branch=master)](https://travis-ci.org/gobby/libinfinity)

libinfinity is library to build collaborative text editors. Changes to
the text buffers are synced to all other clients over a central server.
Even though a central server is involved, the local user sees his
changes applied instantly and the merging is done on the individual
clients.

## Components

infinote consists of the following parts:

- **libinfinity:**
  Library implementing the core infinote part
- **infinoted:**
  Simple stand-alone infinote server application
- **libinfgtk:**
  Provides glib main loop integration and GTK+ widgets related to libinfinity
- **libinftext:**
  Infinote plugin implementing real-time collaborative text editing
- **libinftextgtk:**
  Implements the InfTextBuffer interface with a GtkTextBuffer as backend
- **libinfinoted-plugin-manager:**
  Provides an interface to infinoted for plugins.

## Requirements

libinfinity requires:

- glib-2.0 >= 2.16
- gobject-2.0 >= 2.16
- gthread-2.0 >= 2.16
- libxml-2.0
- gnutls >= 2.12.0
- gsasl >= 0.2.21
- avahi (optional)

infinoted:

- libinfinity
- libdaemon (optional)

libinfgtk:

- libinfinity
- gtk+-3.0 >= 3.2

libinftext:

- libinfinity

libinftextgtk:

- libinftext
- gtk+-3.0 >= 3.2

## Development

This library is developed by Armin Burgmeier <armin@arbur.net>. Artwork is
done by Benjamin Herr <ben@0x539.de>. To get in contact with the developers,
either use the mailing list obby-users@list.0x539.de (to which you can
subscribe by sending mail to obby-users-subscribe@list.0x539.de) or drop by
in our IRC channel #infinote on irc.freenode.org. Feel free to clone
this GitHub repository and propose pull requests. Issues can be reported
to the [GitHub issue tracker](https://github.com/gobby/libinfinity/issues).
