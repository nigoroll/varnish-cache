..
	Copyright (c) 2013-2019 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. _users_performance:

Vinyl Cache and Website Performance
===================================

This section focuses on how to tune the performance of your Vinyl Cache server,
and how to tune the performance of your website using Vinyl Cache.

The section is split in three subsections. The first subsection deals with the
various tools and functions of Vinyl Cache that you should be aware of. The next
subsection focuses on the how to purge content out of your cache. Purging of
content is essential in a performance context because it allows you to extend
the *time-to-live* (TTL) of your cached objects. Having a long TTL allows
Vinyl Cache to keep the content in cache longer, meaning Vinyl Cache will make fewer
requests to your relatively slower backend.

The final subsection deals with compression of web content. Vinyl Cache can
gzip content when fetching it from the backend and then deliver it
compressed. This will reduce the time it takes to download the content
thereby increasing the performance of your website.

.. toctree::
   :maxdepth: 2

   increasing-your-hitrate
   purging
   compression
