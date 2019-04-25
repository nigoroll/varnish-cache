#!/bin/bash

# this script is used by @nigoroll to update this branch and the merged branches

typeset -ra branches=(
    VRT_DirectorResolve
    vtim_faster_printf
    prio_class_reserve
    sess_more_timeouts
    v1l_reopen
    mgmt_vcl_state
    VRT_Format_Proxy
    VNUMpfxint
    instance_info
    sess_more_timeouts
    director_error
)
typeset -ra branches_norebase=(
    proxy_via_6
)

typeset -r this_branch="$(git symbolic-ref --short HEAD)"

typeset -r upstream_remote="origin"
typeset -r my_remote="nigoroll"


typeset -r save=/tmp/varnish_${this_brnach}.save.$$
typeset -ra save_files=(
    README.rst
    update_this_branch.sh
)

set -eux
rm -rf "${save}"
mkdir -p "${save}"
cp "${save_files[@]}" "${save}"
git checkout master
#git pull
git reset --hard "${upstream_remote}"/master
git checkout "${this_branch}"
git reset --hard master
for b in "${branches[@]}" "${branches_sep[@]}" ; do
    git checkout "${b}"
    git rebase master
    #git push -f "${my_remote}"
done
git checkout "${this_branch}"
for b in "${branches[@]}" "${branches_norebase[@]}" ; do
	if ! git merge --no-ff -m "merge $b" $b ; then
		echo SUBSHELL TO FIX
		bash
	fi
done
cp "${save}"/* .
git add "${save_files[@]}"
git commit -am 'rebased and remerged'
rm -rf "${save}"
set +x
echo
echo DONE. when happy, issue:
echo
echo git push -f "${my_remote}"
