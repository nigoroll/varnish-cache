#!/bin/bash

cp -p $0 /tmp/save.$$

set -eux
git reset --hard master
if ! git merge 'VRT_DirectorResolve' 'vtp_preamble'; then
	echo SHELL to resolve conflict
	bash
fi
commits=(
    # add an ipv6 bogo ip by the name b...
    0ebc3669c4b933e468118a85966b444f1c687cfd
    # Basic "via" backends support
    9ccde54d706817bc0267cd24a6debba11c328a79
    # via backends in VCL
    428baea7a1a272c93b5eb1e7bbaca4d11e2245f9
    # Add the .authority field to backend...
    7abe36234964e78b4e94bad89360d0a59942b0a8
)
for c in "${commits[@]}" ; do
    if ! git cherry-pick "${c}" ; then
	echo SHELL to resolve conflict
	bash
    fi
done

cp -p /tmp/save.$$ $0
git add $0
git commit -m 'add the update script'
