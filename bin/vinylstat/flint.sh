#!/bin/sh
#
# Copyright (c) 2010-2021 Varnish Software AS
# SPDX-License-Identifier: BSD-2-Clause
# See LICENSE file for full text of license

FLOPS='
	vinylstat.c
	vinylstat_curses.c
	vinylstat_curses_help.c

	../../lib/libvinylapi/flint.lnt
	../../lib/libvinylapi/*.c
' ../../tools/flint_skel.sh $*
