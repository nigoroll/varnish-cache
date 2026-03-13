..
	Copyright (c) 2012-2015 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _users-guide-index:

The Vinyl Cache Users Guide
===========================

The Vinyl Cache documentation consists of three main documents:

* :ref:`tutorial-index` explains the basics and gets you started with Vinyl Cache.

* :ref:`users-guide-index` (this document), explains how Vinyl Cache works
  and how you can use it to improve your website.

* :ref:`reference-index` contains hard facts and is useful for
  looking up specific questions.

After :ref:`users_intro`, this Users Guide is organized in sections
following the major interfaces to Vinyl Cache as a service:

:ref:`users_running` is about getting Vinyl Cache configured, with
respect to storage, sockets, security and how you can control and
communicate with Vinyl Cache once it is running.

:ref:`users_vcl` is about getting Vinyl Cache to handle the
HTTP requests the way you want, what to cache, how to cache it,
modifying HTTP headers etc. etc.

:ref:`users_report` explains how you can monitor what Vinyl Cache does,
from a transactional level to aggregating statistics.

:ref:`users_performance` is about tuning your website with Vinyl Cache.

:ref:`users_trouble` is for locating and fixing common issues with Vinyl Cache.

.. toctree::
   :maxdepth: 2

   intro
   running
   vcl
   report
   performance
   esi
   troubleshooting

.. customizing (which is a non ideal title)

.. No longer used:

        configuration
        command_line
        VCL
	backend_servers
	logging
        sizing_your_cache
        statistics
        increasing_your_hitrate
	cookies
	vary
        hashing
	purging
	compression
	esi
	websockets
	devicedetection
        handling_misbehaving_servers
        advanced_topics
	troubleshooting

