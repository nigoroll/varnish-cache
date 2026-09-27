Vinyl Cache
===========

This is Vinyl Cache, the high-performance HTTP accelerator with
additional features and fixes.

Why this repository is still on github with a wrong name
--------------------------------------------------------

I just did not get to it.

How this Repository is Organized
--------------------------------

The ``unmerged_code`` branch is created by merging feature/bug fix
branches onto `Vinyl Cache main`_. These branches are usually for
`Vinyl Cache Pull Requests`_.

.. _Vinyl Cache main: https://code.vinyl-cache.org/vinyl-cache/vinyl-cache/src/branch/main
.. _Vinyl Cache Pull Requests: https://code.vinyl-cache.org/vinyl-cache/vinyl-cache/pulls?q=&type=all&state=open&labels=1314&milestone=0&assignee=0&poster=0

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

Documentation and additional information about Vinyl Cache is available on
https://vinyl-cache.org/

Please see CONTRIBUTING for how to contribute patches and report bugs.
