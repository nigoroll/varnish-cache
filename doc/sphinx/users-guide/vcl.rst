..
	Copyright (c) 2012-2016 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license


.. _users_vcl:

VCL - Vinyl Configuration Language
----------------------------------

This section covers how to tell Vinyl Cache how to handle
your HTTP traffic, using the Vinyl Configuration Language (VCL).

Vinyl Cache has a great configuration system. Most other systems use
configuration directives, where you basically turn on and off lots of
switches. We have instead chosen to use a domain specific language called VCL for this.

Every inbound request flows through Vinyl Cache and you can influence how
the request is being handled by altering the VCL code. You can direct
certain requests to particular backends, you can alter the requests and
the responses or have Vinyl Cache take various actions depending on
arbitrary properties of the request or the response. This makes
Vinyl Cache an extremely powerful HTTP processor, not just for caching.

Vinyl Cache translates VCL into binary code which is then executed when
requests arrive. The performance impact of VCL is negligible.

The VCL files are organized into subroutines. The different subroutines
are executed at different times. One is executed when we get the
request, another when files are fetched from the backend server.

If you don't call an action in your subroutine and it reaches the end
Vinyl Cache will execute some built-in VCL code. You will see this VCL
code commented out in the file `builtin.vcl` that ships with Vinyl Cache.

.. _users-guide-vcl_fetch_actions:

.. toctree::
   :maxdepth: 1

   vcl-syntax
   vcl-built-in-code
   vcl-variables
   vcl-backends
   vcl-hashing
   vcl-grace
   vcl-separate
   vcl-inline-c
   vcl-examples
   devicedetection

