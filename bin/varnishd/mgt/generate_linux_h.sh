#!/bin/bash

tmp_file=$(mktemp)

#echo "LOAD_SYSCALL_NR, \\" >> $tmp_file
echo "static int rc = -1; \\" >> $tmp_file

for syscall in $(egrep "^type=" audit | awk '{print$13}' | sort -u | awk -F'=' '{print$2}' | xargs -i grep -w "{}" syscall_list | awk '{print$2}')
do
	echo "rc = seccomp_rule_add(ctx, SCMP_ACT_LOG, SCMP_SYS(${syscall}), 0); \\" >> ${tmp_file}
done

eval "sed '/place_holder/e cat ${tmp_file}' mgt_jail_linux.h.in" >> mgt_jail_linux.h
sed -i 's/place_holder//g' mgt_jail_linux.h

#rm tmp_file

