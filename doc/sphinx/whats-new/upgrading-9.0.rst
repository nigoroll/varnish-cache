.. _whatsnew_upgrading_9.0:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Upgrading to Vinyl Cache 9.0
%%%%%%%%%%%%%%%%%%%%%%%%%%%%

This document only lists breaking changes that you should be aware of when
upgrading from Varnish Cache 8.x to Vinyl Cache 9.0. For a complete list of
changes, please refer to the `change log`_ and :ref:`whatsnew_changes_9.0`.

.. _change log: https://code.vinyl-cache.org/vinyl-cache/vinyl-cache/src/branch/main/doc/changes.rst

Together with this release, we also announce a security issue, see `vsv18`_.

Name change
===========

.. _`may have noticed`: https://vinyl-cache.org/organization/20-years.html

As you `may have noticed`_, the project name has been changed from *Varnish
Cache* to *Vinyl Cache*, and this is the first release under the new name. All
binaries, documentation, and other references have been updated to reflect this
change. Starting from this release, the main program is now ``vinyld`` instead
of ``varnishd``. Similarly, all tools were renamed accordingly (``vinyllog``,
``vinylncsa``.. etc).

*Vinyl Cache* is the official project name, and we use *vinyl-cache* or
*Vinyl-Cache* with a dash only where a space is not technically possible, such
as in domain names and certain HTTP headers, or breaking with conventions, such
as in filenames. If neither of these work, the last resort is *Vinyl_Cache* or
*vinyl_cache*. Any other spelling should be avoided. A short ASCII
representation of the project name is ``\(``, which resembles our new logo.

Our homepage is now https://vinyl-cache.org

Our mastodon account is https://fosstodon.org/@vinyl_cache

We have left github and the core code repository is now at
https://code.vinyl-cache.org/vinyl-cache/vinyl-cache

For more information on the repository move see
https://vinyl-cache.org/organization/moving.html

While the name change might look mostly cosmetic at first, besides, obviously,
needing to start ``vinyld`` instead of ``varnishd``, and running, for example,
``vinylstat`` instead of ``varnishstat``, there are some less obvious
consequences, so we recommend to read through the following changes. Other than
that, the upgrade should work as usual and, in particular, VCL should require
adjustments only in rare cases.

Affecting multiple programs
===========================

The environment variable ``VARNISH_DEFAULT_N`` is now ``VINYL_DEFAULT_N``

The directory structure has been changed. Sparing details like differences
where ``etc`` and ``lib`` reside, the changes are:

* ``include/varnish`` is now ``include/vinyl-cache``
* ``lib/varnish`` is now ``lib/vinyl-cache``
* ``share/doc/varnish`` is now ``share/doc/vinyl-cache``
* ``share/varnish`` is now ``share/vinyl-cache``
* ``etc/varnish`` is now ``etc/vinyl-cache``
* ``var/varnish`` is now ``var/vinyl-cache`` if used, otherwise the state
  directory is most likely ``/var/run``

Most notable of these is the default VCL location which, for most users, will
change from ``/etc/varnish`` to ``/etc/vinyl-cache``.

vinyld
======

Unix user changes
~~~~~~~~~~~~~~~~~

The Unix user and group used by the ``unix`` and ``linux`` jails have been
changed from ``varnish`` to ``vinyl``. Users transitioning from Varnish Cache
8.0 might find this snippet helpful to delete and create the relevant
users/groups (see also `vinyld(1)`)::

  userdel varnish || true
  userdel vcache || true
  userdel varnishlog || true # to remove reference to varnish group
  groupdel varnish || true

  groupadd vinyl
  useradd -g vinyl -d /nonexistent -s /bin/false \
    -c "Vinyl Cache Daemon User" vinyl
  useradd -g vinyl -d /nonexistent -s /bin/false \
    -c "Vinyl Cache Worker User" vcache
  useradd -g vinyl -d /nonexistent -s /bin/false \
    -c "Vinyl Log User" vinyllog

Path changes
~~~~~~~~~~~~

The default ``vcl_path`` has changed

* from: ``${sysconfdir}/varnish:${datadir}/varnish/vcl``
* to: ``${sysconfdir}/vinyl-cache:${datadir}/vinyl-cache/vcl``

So, most notably, for a default installation, the location for VCL files is
now ``/etc/vinyl-cache``.

The default ``vmod_path`` has changed

* from: ``${libdir}/varnish/vmods``
* to: ``${libdir}/vinyl-cache/vmods``

Default header changes
~~~~~~~~~~~~~~~~~~~~~~

The default ``Server`` and ``Via`` headers have been changed from ``Varnish``
to ``Vinyl-Cache``.

The ``X-Varnish`` header is now ``X-Vinyl``

.. _VSV00018: https://vinyl-cache.org/security/VSV00018.html

.. _vsv18:

`VSV00018`_
~~~~~~~~~~~

The handling of HTTP/1.1 requests to an "absolute form" URI has been fixed to
also cover the case where the absolute form has an empty path component:

Previously, a request with an empty path like ``GET http://example.com
HTTP/1.1`` would cause ``req.url`` to contain ``http://example.com`` and the
``Host:`` header to remain unchanged. This has now been fixed:

- ``req.url`` gets set to ``*`` if the request method is ``OPTIONS`` and to
  ``/`` otherwise

- The ``Host:`` header gets set to ``example.com``.

For an empty path with query parameters like ``http://example.com?/foo``,
``req.url`` gets normalized by addition of the leading slash. For the example,
``req.url`` would contain ``/?/foo``.

For requests to an absolute form URI, the host field is now required. Requests
without a host field are rejected with a Status 400 error.

The built-in VCL has been changed to require ``req.url`` to start with ``/``,
unless the request method is ``CONNECT`` or ``OPTIONS``. For ``CONNECT``, no
additional check is applied, but ``CONNECT`` is not allowed by default. For
``OPTIONS``, ``*`` is also allowed.

VCL variable ``beresp.storage_hint`` removed
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The VCL variable ``beresp.storage_hint`` has been removed. If you were using
this variable in your VCL, you will need to remove any references to it.

VCL variable ``req.ttl`` deprecated
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``req.ttl`` variable has been renamed to ``req.max_age`` for clarity.
``req.ttl`` is retained as an alias and continues to work, but is now deprecated
and will be removed in a future version of Vinyl Cache. You should update your
VCL to use ``req.max_age`` instead.

Content-Length handling for requests without body
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For requests having no request body, the ``Content-Length`` header will now
only be unset when the request method is one of: ``GET``, ``HEAD``, ``DELETE``,
``OPTIONS``, ``TRACE``. For other methods, a ``Content-Length`` header with
value 0 will be set instead. This may affect backends that are sensitive to
the presence of the ``Content-Length`` header.

vinylncsa
=========

In ``vinylncsa`` formats, ``%{Varnish:....}`` has been replaced with
``%{Vinyl:....}``

Upgrade notes for VMOD developers
=================================

Name change
~~~~~~~~~~~

The ``libvarnishapi`` library is now ``libvinylapi``.

The pkg-config file ``varnishapi.pc`` is now ``vinylapi.pc``.

The autoconf macro file ``varnish.m4`` is now ``vinyl.m4``, and the
``VARNISH_*`` macros are now called ``VINYL_*``.

In ``vsc`` files, ``varnish_vsc`` is now ``vinyl_vsc``.

On a git repository with an autotools-based VMOD where file names do not contain
white space, the following one-liner should produce at least haft-decent
results::

        sed -i 's/varnishtest/vtest/g;s/varnish/vinyl/g;s/VARNISH/VINYL/g;s/Varnish.Cache/Vinyl Cache/g;s/Varnish/Vinyl Cache/g;' $(git ls-files)

Please note that these results should only be taken as the basis for manual
edits!

The most important changes are:

* In ``configure.ac``, replace ``VARNISH_`` macros with ``VINYL_``.
* In ``Makefile.am``\ s, replace ``VARNISHAPI_`` with ``VINYLAPI_`` and
  ``VARNISH_`` with ``VINYL_``.
* In ``vtc`` files, replace ``varnishtest`` with ``vtest`` and ``varnish`` with
  ``vinyl``.

``VSL_Setup()`` replaced with ``VSL_Init()`` and ``VSL_Alloc()``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``VSL_Setup()`` has been replaced with two new functions:

- ``VSL_Init()`` to initialize caller-provided space as a VSL buffer
- ``VSL_Alloc()`` to allocate the default ``vsl_buffer`` on the heap

``VSL_Free()`` has been added to free the memory allocated by ``VSL_Alloc()``.

The coccinelle script ``tools/coccinelle/vsl_setup_retire.cocci`` can be used
to partially automate the transition (it does not add ``VSL_Free()`` calls).

*eof*
