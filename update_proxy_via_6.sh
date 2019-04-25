#!/bin/bash

cp -p $0 /tmp/save.$$

set -eux
git reset --hard master
git merge 'VRT_DirectorResolve' 'VRT_Format_Proxy'
commits=(
	9c8846c4065a1006780f0c897f1fff8c764c8d4c
	8c7d3e42138f8253a8bd8632a93170ee0456b67d
	660cf1f2c4d679079915feb18204d4679bce8053
	2ce51afa3631948ae3a7075b7d27893d06f1a897
	612957fbd782913ed787573ef715082b8f8517ff
	a1721fcb8797c78e0148601104dcaebe7b277cb6
	)
git cherry-pick "${commits[@]}"

cp -p /tmp/save.$$ $0
git add $0
git commit -m 'add the update script'
