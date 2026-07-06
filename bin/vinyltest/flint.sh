#!/bin/sh
#
# Copyright (c) 2008-2021 Varnish Software AS
# SPDX-License-Identifier: BSD-2-Clause
# See LICENSE file for full text of license

FLOPS="
	-DVTEST_WITH_VTC_LOGEXPECT
	-DVTEST_WITH_VTC_VINYL
	-DTOP_BUILDDIR="foo"
	-I../../lib/libvgz
	-Ivtest2/lib
	-Ivtest2/src
	$(ls vtest2/src/*.c| egrep -v '/teken.|vtc_varnish')
	vtc_vinyl.c
" ../../tools/flint_skel.sh
