Varnish Cache
=============

This is Varnish Cache, the high-performance HTTP accelerator.

master + additional branches / PRs merged.

*NOTE* that this branch gets force-pushed to github, so users who do
not re-clone with every use should run::

  $origin=${remote}
  git pull
  git reset --hard ${remote}/unmerged_code

to update with ``$origin`` set appropriately to your remove name.

Additional code:

	* VRT_DirectorResolve / https://github.com/varnishcache/varnish-cache/pull/2680
	* vtim_faster_printf / https://github.com/varnishcache/varnish-cache/pull/2792
	* prio_class_reserve / https://github.com/varnishcache/varnish-cache/pull/2796
	* sess_more_timeouts / https://github.com/varnishcache/varnish-cache/pull/2983
	* v1l_reopen / https://github.com/varnishcache/varnish-cache/pull/2803
	* mgmt_vcl_state / https://github.com/varnishcache/varnish-cache/pull/2836
	* VRT_Format_Proxy / https://github.com/varnishcache/varnish-cache/pull/2845
	* director_error / https://github.com/varnishcache/varnish-cache/pull/2939
	* VNUMpfxint / https://github.com/varnishcache/varnish-cache/pull/2929
	* instance_info / https://github.com/varnishcache/varnish-cache/pull/2966
	* proxy_via_6 / https://github.com/varnishcache/varnish-cache/pull/2957
	* sess_more_timeouts / https://github.com/varnishcache/varnish-cache/pull/2983

Documentation and additional information about Varnish is available on
https://www.varnish-cache.org/

Technical questions about Varnish and this release should be addressed
to <varnish-misc@varnish-cache.org>.

Please see CONTRIBUTING for how to contribute patches and report bugs.

Questions about commercial support and services related to Varnish
should be addressed to <sales@varnish-software.com>.
