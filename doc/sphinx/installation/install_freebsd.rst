..
	Copyright (c) 2026 Poul-Henning Kamp
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _install-freebsd:

Installing on FreeBSD
=====================

As package
----------

Install as you would any other package:

	pkg install vinyl09

where "09" is the major version.

From ports
----------

The FreeBSD packages are built out of the "ports" tree, and you can
install Varnish Cache directly from ports if you prefer, for instance to
get a newer version of Varnish Cache than the current set of prebuilt
packages provide::

	cd /usr/ports/www/vinyl09
	make all install clean

The only option of this port is if documentation is built or not.
