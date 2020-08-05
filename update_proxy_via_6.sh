#!/bin/bash

cp -p $0 /tmp/save.$$

set -eux
git checkout vtp_preamble
git rebase master
git checkout proxy_via_6_vtp-preamble
git rebase vtp_preamble

cp -p /tmp/save.$$ $0
git add $0
git commit -m 'add the update script'
