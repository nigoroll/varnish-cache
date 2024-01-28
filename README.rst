Varnish Cache
=============

This is Varnish Cache, the high-performance HTTP accelerator with
additional features and fixes.

How this Repository is Organized
--------------------------------

The ``unmerged_code`` branch is created by merging feature/bug fix
branches onto `Varnish Cache master`_. These branches are usually for
`Varnish Cache Pull Requests`_.

.. _Varnish Cache master: https://github.com/varnishcache/varnish-cache/tree/master
.. _Varnish Cache Pull Requests: https://github.com/varnishcache/varnish-cache/pulls
*NOTE* The ``unmerged_code`` branch gets force-pushed to
github. Individual releases of this repository are published as
branches named ``unmerged_code_``\ *<YYYY><mm><dd>*\ ``_``\
*<HH><MM><SS>*.

* Users wishing to use the lastest code should run::

  $origin=${remote}
  git pull
  git reset --hard ${remote}/unmerged_code

* Alternatively, pull the respective release branch.

VMOD Compatibility
------------------

Changes merged in this branch might break existing interfaces and thus
require changes to VMODs. We make sure that this branch works with a
set of VMODs and create branches where necessary.

The list of VMODs which we support and the respective repositories and
branches is contained in the file ``VMODS.json``.

When we create a release branch, we add ``VMODS.commits.json`` to
contain a list of VMOD commit ids which have been successfully built
with the release branch.


General Information
-------------------

Documentation and additional information about Varnish is available on
https://www.varnish-cache.org/

Technical questions about Varnish and this release should be addressed
to <varnish-misc@varnish-cache.org>.

Please see CONTRIBUTING for how to contribute patches and report bugs.
