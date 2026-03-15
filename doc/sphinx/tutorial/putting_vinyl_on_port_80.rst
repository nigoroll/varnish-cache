..
	Copyright (c) 2010-2017 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license


Put Vinyl Cache on port 80
--------------------------

Until now we've been running with ``vinyld`` on a high port which is great for
testing purposes. Let's now put ``vinyld`` on the default HTTP port 80.

First we stop ``vinyld``: ``service vinyl stop``

Now we need to edit the configuration file that starts ``vinyld``.

Debian/Ubuntu (legacy)
~~~~~~~~~~~~~~~~~~~~~~

On older Debian/Ubuntu this is `/etc/default/vinyl`. In the file you'll find
some text that looks like this::

  DAEMON_OPTS="-a :6081 \
               -T localhost:6082 \
               -f /etc/vinyl-cache/default.vcl \
               -S /etc/vinyl-cache/secret \
               -s default,256m"

Change it to::

  DAEMON_OPTS="-a :80 \
               -T localhost:6082 \
               -f /etc/vinyl-cache/default.vcl \
               -S /etc/vinyl-cache/secret \
               -s default,256m"

Debian (v8+) / Ubuntu (v15.04+)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

On more recent Debian and Ubuntu systems this is configured in the systemd
service file.

Applying changes to the default service is best done by creating a new file
`/etc/systemd/system/vinyl.service.d/customexec.conf`::

  [Service]
  ExecStart=
  ExecStart=/usr/sbin/vinyld -a :80 -T localhost:6082 -f /etc/vinyl-cache/default.vcl -S /etc/vinyl-cache/secret -s default,256m

This will override the ExecStart part of the default configuration shipped
with Vinyl Cache.

Run ``systemctl daemon-reload`` to make sure systemd picks up the new
configuration before restarting ``vinyld``.


Red Hat Enterprise Linux / CentOS
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

On Red Hat/CentOS you can find a similar configuration file in
`/etc/sysconfig/vinyl`.


Restarting ``vinyld`` again
---------------------------

Once the change is done, restart ``vinyld``: ``service vinyl start``.

Now everyone accessing your site will be accessing through ``vinyld``.
