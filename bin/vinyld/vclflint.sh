#!/bin/sh
#
# Run flexelint on the VCL output
LIBS="-p vmod_path=/home/phk/Vinyl/trunk/vinyl-cache/vmod/.libs"

if [ "x$1" = "x" ] ; then
	./vinyld $LIBS -C -b localhost > /tmp/_.c
elif [ -f $1 ] ; then
	./vinyld $LIBS -C -f $1 > /tmp/_.c
else
	echo "usage!" 1>&2
fi

flexelint vclflint.lnt /tmp/_.c
