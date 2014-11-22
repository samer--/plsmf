plsmf
=====

SWI Prolog library for reading standard MIDI files

This foreign library and module allows Prolog code to read and write standard
MIDI files (SMFs) using libsmf.


##PREREQUISITES

- SWI Prolog
- libsmf ( http://sourceforge.net/projects/libsmf/ or use your package manager )
- glib-2.0 ( probably installable using your system's package manager )


##INSTALLATION

The Makefile uses pkg-config to find glib, which seems to work fine on Debian
and probably other Linux distributions, and on Mac OS X with Macports.

libsmf does not seem to have a pkg-config file, the configure script in this
package uses locate (or mdfind on Mac OS X) to look for `libsmf.a`. It then assumes
that libsmf.a is in `$SMFPREFIX/lib/` and works out `SMFPREFIX` from that. If that
doesn't work, it just assumes that `SMFPREFIX` should be `/usr/local`.

If all the prequisites are satisfied and libsmf is findable, then you should be
able to install this package from the SWI Prolog command line using 

	?- pack_install(plsmf).


##CHANGE LOG

v0.1   - 	initial release.
v0.2.0 - 	initial pack version.
