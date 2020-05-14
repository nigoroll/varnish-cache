#!/bin/bash

# this script is used by @nigoroll to update this branch and the merged branches

# also contains:
# * proxy_via_6_vtp-preamble	# #3128
#    * vtp_preamble		# #3149

# to fetch
typeset -ra remotes=(
    Dridi
    andrewwiik
    daghf
    gquintard
    hermunn
    martin-g
    mbgrydeland
    nigoroll
    scn
    slimhazard
    stevendore
)
typeset -ra branches_norebase=(
    proxy_via_6_vtp-preamble

    # other people's PRs
    stevendore/beresp.fail_reason	# #3113
    Dridi/issue_3114			# #3123
    Dridi/_type				# #3158
    daghf/r03266			# #3267
    mbgrydeland/master-exprace		# #3261
    mbgrydeland/master-vtim-format	# #3308

)
typeset -ra branches=(
    v1l_reopen			# old PR was turned down
    vtp_preamble		# #3149
    acl_merge			# PR TODO
    VNUMpfxint			# #2929
    rfc_call			# #3167
    improve_vcl_caching		# #3245
    vrt_filter_err		# #3287
    fix-out-of-tree-doc-build	# #3310
    vdp_end_with_last_bytes	# #3298
    VDI_Resolve_errors		# #3315
    ws_highwater		# #3285
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
cp "${save_files[@]}" "${save}"
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
cp "${save}"/* .
git add "${save_files[@]}"
git commit -am 'rebased and remerged'
n=$(date +%Y%m%d_%H%M%S)
git branch unmerged_code_"${n}"
git push "${my_remote}" unmerged_code_"${n}"
rm -rf "${save}"
set +x
echo
echo DONE. when happy, issue:
echo
echo git push -f "${my_remote}"
