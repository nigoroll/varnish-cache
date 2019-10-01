.. _dev-guide-index:

The Varnish Developers Guide
============================

This is the deliberately short and to the point list of things
Varnish Developers should know.

Technical stuff
----------------

* Our coding style guideline is FreeBSD's
  `style(9) <https://www.freebsd.org/cgi/man.cgi?query=style&sektion=9>`_

* See autogen.des script for developer options to the toolchain.

* We always -Werror, there are no harmless warnings, only source code
  that does not express intent well enough.

* We prefer the source code, rather than the comments explain what is
  going on, that way tools like FlexeLint and Coverity also gets a chance.

* Our reference platforms are Ubuntu and FreeBSD.

* Asserts have negative cost, they save developer time next time around.

* Our license is BSD 2-clause or looser, no GPL or LGPL.

* Writing secure code is more important than performance.

Bugs, issues, feature requests & VIPs
-------------------------------------

Bugs, issues and feature requests start out as github issues.

Monday at 15:00-15:30 (EU time) we "bug-wash" on IRC to decide who and
how issues are dealt with.

Issues we cannot do anything about are closed.

If feature requests make sense, they get moved to a wiki/VIP page until
somebody implements them.

Varnishtest cases for bugs is the norm, not the exception.

Pull requests should be reviewed by core developers (defined as
developers with significant contributions the last year) within
reasonable time after they are created. The reviews should be carried
out by at least one developer from a different company, to ensure a
minimal amount of independence.

Special developers, called *maintainers*, are appointed (see
`policy-overnance`_) to approve or veto pull requests.

If two maintainers have OK'ed a PR, and there is no veto from another
maintainer, then the PR can be merged.

The exception is small, *risk free* changes, which can be commited and
pushed (by a developer with the *commit bit*) without a
review. Documentation can also be pushed, unless it concerns policies
or governance of the project.

Architectural stuff
-------------------

These rules are imported from the X11 project:

* It is as important to decide what a system is not as to decide what it is.

* Do not serve all the world's needs; rather, make the system extensible so
  that additional needs can be met in an upwardly compatible fashion.

* The only thing worse than generalizing from one example is generalizing
  from no examples at all.

* If a problem is not completely understood, it is probably best to provide
  no solution at all.

* If you can get 90 percent of the desired effect for 10 percent of the work,
  use the simpler solution.

* Isolate complexity as much as possible.

* Provide mechanism, rather than policy.

Various policies
----------------

.. toctree::
	:maxdepth: 1

	policy_governance
	policy_vmods

The varnish-cache.org homepage
------------------------------

.. toctree::
	:maxdepth: 1

	homepage_dogfood
	homepage_contrib

Project metadata
----------------

.. toctree::
	:maxdepth: 1

	who
