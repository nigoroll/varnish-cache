..
	Copyright (c) 2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _install-freebsd:

Installing on FreeBSD
=====================

.. XXX needs to be updated for Vinyl

From package
------------

FreeBSD offers Varnish Cache pre-packaged::

	pkg install varnish7

From ports
----------

The FreeBSD packages are built out of the "ports" tree, and you can
install Varnish Cache directly from ports if you prefer, for instance to
get a newer version of Varnish Cache than the current set of prebuilt
packages provide::

	cd /usr/ports/www/varnish7
	make all install clean

