#!/bin/bash

set -x

for syscall in $(egrep "^type=" $1 | awk '{print$13}' | awk -F '=' '{print$2}' | sort -nu )
do
	grep -q ${syscall} $2

	if [ ! $? -eq 0 ]
	then
		echo ${syscall} >> $2
	fi
done
