.. _whatsnew_changes_9.1:

%%%%%%%%%%%%%%%%%%%%%%%%%%%%
Changes in Vinyl Cache 9.1.0
%%%%%%%%%%%%%%%%%%%%%%%%%%%%

This release can be summarized as **performance improvements, improved protocol
handling and bugfixes**. We tackled some long standing issues and have achieved
remarkable performance improvements in some specific areas.

When upgrading, we strongly recommend reading :ref:`whatsnew_upgrading_9.1`,
because some changes should not remain unnoticed. We did not consider any of
them breaking enough to warrant a major version bump, but they might sill be
relevant.

In general, this guide only mentions bug fixes which we deem relevant for more
than a negligible fraction of the user base. Also we do not mention here changes
relevant only for developers working very closely with core code (as, for
example, with storage engines). A more detailed and technical account of changes
in Vinyl Cache, with links to issues that have been fixed and pull requests that
have been merged, may be found in the `change log`_.

.. _change log: https://code.vinyl-cache.org/vinyl-cache/vinyl-cache/src/branch/main/doc/changes.rst


vinyld
======

The ``-x parameter-json`` has been added to output the parameter documentation
in JSON format.

Worker pools are now shut down during a worker process stop as initiated by
``vinyladm stop``. This improves shutdown speed by releasing VCL references
earlier.

The special storage name ``Synth``, as configured through the ``-sSynth=...``
``vinyld`` startup parameter is now the default ``resp.storage`` used for
synthetic responses created in ``vcl_synth{}``. Its default value is ``synth``
as if configured by ``-sSynth=synth``, a newly added special purpose storage
engine with improved performance.

Parameters
~~~~~~~~~~

The parameters ``max_restarts`` and ``max_retries`` gained upper limits of 32766
and 65534, respectively. Note that these limits are far off any sensible values
for practical purposes, which should rather be in the order of 1-10.

The ``drop_pools`` experimental parameter has been removed.

Protocol framing fixes
~~~~~~~~~~~~~~~~~~~~~~

We fixed spurious HTTP/2 ``PROTOCOL_ERROR`` connection errors caused by
read-ahead data being lost.

HTTP/1 message framing checks have been tightened:

- Backend responses with invalid body framing now fail the fetch.

- Multiple ``Transfer-Encoding`` fields are now considered as a whole, such that
  duplicate ``chunked`` codings are refused.

- Multiple ``Content-Length`` fields and values are now accepted if they all
  agree (leading zeroes are tolerated) and consolidated into a single canonical
  header, any disagreement is refused.

- ``Transfer-Encoding`` on an HTTP/1.0 request is now refused. For HTTP/1.0
  backend responses, the new ``vcl_beresp_http10`` built-in subroutine abandons
  the fetch, which VCL can override.

- The backend connection is now closed when the range check of a response
  fails, to avoid reusing a connection with an unread body.

- An existing, correct ``Connection`` response header is no longer overwritten
  when closing: other tokens are preserved and ``close`` or ``keep-alive`` are
  added as needed. If a ``Connection`` was not present, or was incorrect, we
  create a new, correct one.

BAN improvements
~~~~~~~~~~~~~~~~

The ``obj.hfm``, ``obj.hfp``, and ``obj.uncacheable`` ban variables have been
added to allow banning hit-for-miss and hit-for-pass and uncachable (both)
objects.

New VCL variables
=================

The ``req0.method``, ``req0.url``, ``req0.proto`` and ``req0.http.*`` variables
have been added, providing read access to the request almost exactly as it was
originally received.

The ``esi_ignore_https``, ``esi_disable_xml_check``,
``esi_ignore_other_elements`` and ``esi_remove_bom`` feature flags can now be
overridden per response with the new ``beresp.esi_ignore_https``,
``beresp.esi_disable_xml_check``, ``beresp.esi_ignore_other_elements`` and
``beresp.esi_remove_bom`` variables.

The new ``resp.esi_include_onerror`` variable allows to override the
``esi_include_onerror`` feature flag in ``vcl_deliver {}``.

A new ``resp.storage`` VCL variable available from ``vcl_synth{}`` was added to
select which storage the synth response body gets created on.

Other changes to VCL
~~~~~~~~~~~~~~~~~~~~

Range processing is now only applied to ``GET`` requests as mandated by RFC
9110, and the builtin VCL removes the ``Range`` header from other requests.

The HTTP ``QUERY`` method has been added to the well known request methods
and is passed by default in the builtin VCL.

Changes to bundled VMODs
========================

``std.getenv()`` gained an optional *fallback* argument which is returned if
the environment variable is not set.

The argument to ``std.rollback()`` is now obsolete and ignored, the appropriate
headers to be rolled back are now inferred from the call site. This also fixes
a panic when ``resp`` or ``beresp`` was passed.

``std.collect_all()`` has been added to combine all multiple headers of
``req``, ``resp``, ``bereq`` or ``beresp`` according to the HTTP RFCs:
``Set-Cookie`` is not combined, ``Cookie`` is combined with ``"; "`` and all
other headers with ``", "``.

VSL (vinyllog, vinylncsa)
=========================

These changes concern log processing in general, so they also affect other VSL
processing tools.

``HttpGarbage`` log records now contain the offending data rather than just
the request method.

HTTP/1 messages rejected for invalid body framing now get a ``BogoHeader`` log
record naming the offending field.

vinyladm
========

The ``-x workdir`` option has been added to ``vinyladm`` to print the default
work directory and exit. This is useful for tools that need to discover the VSM
location in most setups.

vinyllog, vinylncsa, vinylhist, vinyltop
========================================

These tools all gained the ``-0`` dry-run argument that allows validating
command line arguments before running them for real.

VSC (vinylstat)
===============

These changes concern counters in general, so they also affect other VSC
processing tools.

Counters for backend connection closes have been added:
``MAIN.backend_closed``, ``MAIN.backend_closed_err`` and the ``MAIN.bc_*``
close reason counters that are a subset of ``sc_*`` session counters, as well
as the equivalent per-backend ``VBE.*.closed``, ``VBE.*.closed_err`` and
close reason counters.

.. _whatsnew_changes_9.1_vtest:

vinyltest / vtest
=================

Vinyl Cache can now be built without the vtest2 git submodule and instead with
an externally provided vtest program, as installed by running ``DESTDIR=/prefix
make install`` in the Vtest2 repository
https://code.vinyl-cache.org/vtest/VTest2

The Vinyl Cache specific test commands `vinyl`, `logexpect` and `vsm` have been
moved to a vtest extension.

For a build *with* the submodule, both a ``vinyltest`` program and ``vtest``
symlink continue to be built and installed as monolithic binaries which always
include the extension.

For a build *without* the submodule (with an external ``vtest`` program), a
``vinyltest`` wrapper is built, which sets up the right command line arguments
and calls ``vtest``.

This is done to hopefully achieve a smooth transition, until we plan to
eventually remove ``vinyltest`` and wholly replace it with ``vtest -E
libvtest_ext_vinyl.so`` (or the equivalent for automake), which is what the
``vinyltest`` wrapper already calls.

The following vtest ``feature`` tests have been moved to ``vcache_builtwith`` in
the Vinyl Cache VTest extension: ``64bit``, ``persistent_storage``,
``coverage``, ``asan``, ``msan``, ``tsan``, ``ubsan``, ``sanitizer``,
``workspace_emulator`` and ``witness``.

In the Vinyl Cache VTest extension, the log output of SLT_F_UNSAFE log records
has been changed to escape unprintable characters as ``\xFF``, *FF* being the
hexadecimal representation of the respective character.

The Vinyl Cache VTest extension also gained the ``VTEST_VINYL_VCL_PREPEND``
environment variable to inject VCL code into all VCL loaded by ``vinyl``
instances.

A simple perl script ``tools/vtclog2logexp.sh`` has been added to help create
``logexp`` commands for ``vtest`` from its output.

Changes for VMOD authors and VMOD packagers
===========================================

VMOD builds
~~~~~~~~~~~

We added infrastructure to easily build VMODs for multiple Vinyl Cache based
projects. Besides changing how VMOD builds discover such projects and configure
themselves appropriately, we also added the ``vcache`` command to the Vinyl
Cache VTest extension.

See :ref:`ref-vmod-vcache` for details on how to migrate.

We also fixed the maxmimum version check in the ``VCACHE_REQUIRE`` and
``VINYL_PREREQ`` macros from inclusive to exclusive, which matches the
original ``VARNISH_PREREQ`` documentation. So ``VCACHE_REQUIRE([vinyl],
[9.0.0], [9.1.0])`` no longer implements "9.0.x OR 9.1.0" but rather "9.0.x".

The ``vinyl-legacy.m4`` macro collection for VMOD builds has been removed. VMODs
should migrate to using the macros from ``vinyl.m4``.

API changes
~~~~~~~~~~~

In ``vrt.h``, ``struct vrt_ctx`` gained the ``http_req0`` member and ``enum
gethdr_e`` gained ``HDR_REQ0``.

VMOD vtc
~~~~~~~~

The ``vtc`` VMOD gained the functions ``storage_revert()``, ``storage_full()``,
``storage_lessspace()``, ``storage_maxspace()``, and ``storage_frag()`` to
simulate error conditions and allocation behavior of storage engines.

The ``debug`` storage engine lost all parameters except ``dinit`` and ``dopen``,
as these are replaced by the above now.

Likewise, the ``debug.fragfetch`` helper parameter has been removed,
``vtc.storage_frag()`` fills its place.

Changes for Vinyl Cache package maintainers
===========================================

Please read `whatsnew_changes_9.1_vtest`_. We now encourage you to package
``vtest`` separately and package Vinyl Cache without the bundled ``vtest`` but,
for the time being, with the ``vinyltest`` wrapper.

``make install`` now honours ``DESTDIR`` when creating ``VINYL_STATE_DIR``. The
default for ``VINYL_STATE_DIR`` has been changed back to
``${localstatedir}/lib/vinyl-cache``.

During build, the new ``configure`` option ``--with-statedir`` now allows to set
the ``VINYL_STATE_DIR`` directly, which is the default for ``VINYL_DEFAULT_N``,
which, in turn, is the default for the ``-n`` argument to `vinyld`` and
``vinyl{log,ncsa,hist,top}``.

We apologize for the back and forth just before/after the 9.0 release and hope
to avoid making such mistakes again in the future. That said, we also value
working more closely with package maintainers, are grateful for your feedback
and thank those who already got involved.

Changes for downstream project maintainers
==========================================

We ask you to adopt the new ``vinyl.m4`` for your project under the name of
your project. This is vital to achieve the goal of enabling VMOD authors to
easily support multiple Vinyl Cache based project.

Please also read :ref:`ref-vmod-vcache` to understand the background.

*eof*
