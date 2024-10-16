#!/bin/bash

# this script is used by @nigoroll to update this branch and the merged branches

# to fetch
typeset -ra remotes=(
    Algunenano
    AlveElde
    Dridi
    asadsa92
    andrewwiik
    bsdphk
    carlosabalde
    daghf
    gquintard
    hermunn
    mbgrydeland
    nigoroll
    rezan
    scance
    scn
    slimhazard
    stevendore
    devnexen
    walid
)

# other people's PRs and own which we do not rebase
typeset -ra branches_norebase=(
#    devnexen/v_cold_attr	#  #3860
# alternative route by phk
#   Dridi/issue_3114			# #3123
# diverged too much and not semantically relevant
#    Dridi/_type				# #3158
# needs rebase
#   Dridi/vtc-tunnel			# #3375
# not ready
#   Dridi/vsl-stable			# #3468
)

typeset -ra branches=(
    v1l_reopen				# old PR was turned down

    ear					# #3729

    req_filters_4			# 4035
    vdire				# 4142
    builtin_neuter_content-range	# 4137
    ws_highwater			# #3285
    fix-strands-compare			# 4148

    #vre_capture_fix		# #3725
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
mv VMODS.json VMODS.json. && \
  jq < VMODS.json. >VMODS.json 'sort_by(.vmod)' && \
  rm VMODS.json.
git add "${save_files[@]}"
git commit -am 'rebased and remerged'
./build.py shell
git add VMODS_BUILT.json
git commit -m 'vmod revisions successfully built' VMODS_BUILT.json
n=$(date +%Y%m%d_%H%M%S)
git checkout -b unmerged_code_"${n}"
git push "${my_remote}" unmerged_code_"${n}"
git checkout unmerged_code
rm -rf "${save}"
set +x
echo
echo DONE. when happy, issue:
echo
echo git push -f "${my_remote}"
