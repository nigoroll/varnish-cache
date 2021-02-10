#!/bin/bash

# this script is used by @nigoroll to update this branch and the merged branches

# to fetch
typeset -ra remotes=(
    Dridi
    andrewwiik
    bsdphk
    daghf
    gquintard
    hermunn
    mbgrydeland
    nigoroll
    rezan
    scn
    slimhazard
    stevendore
)

# other people's PRs and own which we do not rebase
typeset -ra branches_norebase=(
    proxy_via_6_vtp-preamble		# #3128

# alternative route by phk
#   Dridi/issue_3114			# #3123
# diverged too much and not semantically relevant
#    Dridi/_type				# #3158

    mbgrydeland/master-vtim-format	# #3308
    Dridi/builtin-split		# #3484
# needs rebase
#   Dridi/vtc-tunnel			# #3375
# not ready
#   Dridi/vsl-stable			# #3468
)

typeset -ra branches=(
    acl_merge			# PR TODO
    v1l_reopen			# old PR was turned down
    VNUMpfxint			# #2929

    improve_vcl_caching		# #3245
    vrt_filter_err		# #3287
    vpi_wip			# #3515
    call_SUB			# #3516

    ## TO BE RE-DONE
    #ws_highwater		# #3285
)

typeset -r this_branch="$(git symbolic-ref --short HEAD)"

typeset -r upstream_remote="origin"
typeset -r my_remote="nigoroll"


typeset -r save=/tmp/varnish_${this_brnach}.save.$$
typeset -ra save_files=(
    README.rst
    update_this_branch.sh
    VMODS.json
    build.py
    .github/FUNDING.yml
)
set -eux

for r in "${remotes[@]}" ; do
    git fetch $r
done

for b in "${branches[@]}" ; do
    git checkout "${b}"
    git pull
    if ! git rebase master ; then
	echo SUBSHELL TO FIX
	bash
    fi
    git push -f "${my_remote}"
done

git checkout "${this_branch}"

echo -n merge?
read y
if [[ "$y" != "y" ]] ; then
    exit
fi

rm -rf "${save}"
mkdir -p "${save}"
find "${save_files[@]}" | cpio -dump "${save}"
git checkout master
#git pull
git reset --hard "${upstream_remote}"/master
git checkout "${this_branch}"
git reset --hard master
git checkout "${this_branch}"
for b in "${branches_norebase[@]}" "${branches[@]}" ; do
	if ! git merge --no-ff -m "merge $b" $b ; then
		echo SUBSHELL TO FIX
		bash
	fi
done
( cd "${save}" && find "${save_files[@]}" | cpio -dump "${OLDPWD}" )
git add "${save_files[@]}"
git commit -am 'rebased and remerged'
n=$(date +%Y%m%d_%H%M%S)
./build.py
git checkout -b unmerged_code_"${n}"
git add VMODS_BUILT.json
git commit -m 'vmod revisions successfully built' VMODS_BUILT.json
git push "${my_remote}" unmerged_code_"${n}"
git checkout unmerged_code
rm -rf "${save}"
set +x
echo
echo DONE. when happy, issue:
echo
echo git push -f "${my_remote}"
