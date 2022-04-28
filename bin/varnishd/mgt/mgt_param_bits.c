/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <string.h>

#include "mgt/mgt.h"
#include "mgt/mgt_param.h"

#include "vav.h"

#include "vsl_priv.h"

/*--------------------------------------------------------------------
 */

enum bit_do {BSET, BCLR, BTST};

static int
bit(uint8_t *p, unsigned no, enum bit_do act)
{
	uint8_t b;

	p += (no >> 3);
	b = (0x80 >> (no & 7));
	if (act == BSET)
		*p |= b;
	else if (act == BCLR)
		*p &= ~b;
	return (*p & b);
}

/*--------------------------------------------------------------------
 */

static int
bit_tweak(struct vsb *vsb, uint8_t *p, unsigned l, const char *arg,
    const char * const *tags, const char *desc, char sign)
{
	int i, n;
	unsigned j;
	char **av;
	const char *s;

	av = VAV_Parse(arg, &n, ARGV_COMMA);
	if (av[0] != NULL) {
		VSB_printf(vsb, "Cannot parse: %s\n", av[0]);
		VAV_Free(av);
		return (-1);
	}
	for (i = 1; av[i] != NULL; i++) {
		s = av[i];
		if (*s != '-' && *s != '+') {
			VSB_printf(vsb, "Missing '+' or '-' (%s)\n", s);
			VAV_Free(av);
			return (-1);
		}
		for (j = 0; j < l; j++) {
			if (tags[j] != NULL && !strcasecmp(s + 1, tags[j]))
				break;
		}
		if (tags[j] == NULL) {
			VSB_printf(vsb, "Unknown %s (%s)\n", desc, s);
			VAV_Free(av);
			return (-1);
		}
		assert(j < l);
		if (s[0] == sign)
			(void)bit(p, j, BSET);
		else
			(void)bit(p, j, BCLR);
	}
	VAV_Free(av);
	return (0);
}


/*--------------------------------------------------------------------
 */

static int
tweak_generic_bits(struct vsb *vsb, const struct parspec *par, const char *arg,
    uint8_t *p, unsigned l, const char * const *tags, const char *desc,
    char sign)
{
	const char *s;
	unsigned j;

	if (arg != NULL && !strcmp(arg, "default") &&
	    strcmp(par->def, "none")) {
		memset(p, 0, l >> 3);
		return (tweak_generic_bits(vsb, par, par->def, p, l, tags,
		    desc, sign));
	}

	if (arg != NULL && arg != JSON_FMT) {
		if (sign == '+' && !strcmp(arg, "none"))
			memset(p, 0, l >> 3);
		else
			return (bit_tweak(vsb, p, l, arg, tags, desc, sign));
	} else {
		if (arg == JSON_FMT)
			VSB_putc(vsb, '"');
		s = "";
		for (j = 0; j < l; j++) {
			if (bit(p, j, BTST)) {
				VSB_printf(vsb, "%s%c%s", s, sign, tags[j]);
				s = ",";
			}
		}
		if (*s == '\0')
			VSB_cat(vsb, sign == '+' ? "none" : "(all enabled)");
		if (arg == JSON_FMT)
			VSB_putc(vsb, '"');
	}
	return (0);
}

/*--------------------------------------------------------------------
 * The vsl_mask parameter
 */

static const char * const VSL_tags[256] = {
#  define SLTM(foo,flags,sdesc,ldesc) [SLT_##foo] = #foo,
#  include "tbl/vsl_tags.h"
};

static int v_matchproto_(tweak_t)
tweak_vsl_mask(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_bits(vsb, par, arg, mgt_param.vsl_mask,
	    SLT__Reserved, VSL_tags, "VSL tag", '-'));
}

/*--------------------------------------------------------------------
 * The debug parameter
 */

static const char * const debug_tags[] = {
#  define DEBUG_BIT(U, l, d) [DBG_##U] = #l,
#  include "tbl/debug_bits.h"
       NULL
};

static int v_matchproto_(tweak_t)
tweak_debug(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_bits(vsb, par, arg, mgt_param.debug_bits,
	    DBG_Reserved, debug_tags, "debug bit", '+'));
}

/*--------------------------------------------------------------------
 * The experimental parameter
 */

static const char * const experimental_tags[] = {
#  define EXPERIMENTAL_BIT(U, l, d) [EXPERIMENT_##U] = #l,
#  include "tbl/experimental_bits.h"
       NULL
};

static int v_matchproto_(tweak_t)
tweak_experimental(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_bits(vsb, par, arg, mgt_param.experimental_bits,
	    EXPERIMENT_Reserved, experimental_tags, "experimental bit", '+'));
}

/*--------------------------------------------------------------------
 * The feature parameter
 */

static const char * const feature_tags[] = {
#  define FEATURE_BIT(U, l, d) [FEATURE_##U] = #l,
#  include "tbl/feature_bits.h"
       NULL
};

static int v_matchproto_(tweak_t)
tweak_feature(struct vsb *vsb, const struct parspec *par, const char *arg)
{

	return (tweak_generic_bits(vsb, par, arg, mgt_param.feature_bits,
	    FEATURE_Reserved, feature_tags, "feature bit", '+'));
}

/*--------------------------------------------------------------------
 * The parameter table itself
 */

struct parspec VSL_parspec[] = {
	{ "vsl_mask", tweak_vsl_mask, NULL,
		NULL, NULL,
		/* default */
		"-Debug,"
		"-ExpKill,"
		"-H2RxBody,"
		"-H2RxHdr,"
		"-H2TxBody,"
		"-H2TxHdr,"
		"-Hash,"
		"-ObjHeader,"
		"-ObjProtocol,"
		"-ObjReason,"
		"-ObjStatus,"
		"-VdpAcct,"
		"-VfpAcct,"
		"-WorkThread",
		NULL,
		"Mask individual VSL messages from being logged.\n"
		"\tdefault\tSet default value\n"
		"\nUse +/- prefix in front of VSL tag name to unmask/mask "
		"individual VSL messages." },
	{ "debug", tweak_debug, NULL,
		NULL, NULL,
		/* default */
		"none",
		NULL,
		"Enable/Disable various kinds of debugging.\n"
		"\tnone\tDisable all debugging\n\n"
		"Use +/- prefix to set/reset individual bits:"
#define DEBUG_BIT(U, l, d) "\n\t" #l "\t" d
#include "tbl/debug_bits.h"
		},
	{ "experimental", tweak_experimental, NULL,
		NULL, NULL,
		/* default */
		"none",
		NULL,
		"Enable/Disable experimental features.\n"
		"\tnone\tDisable all experimental features\n\n"
		"Use +/- prefix to set/reset individual bits:"
#define EXPERIMENTAL_BIT(U, l, d) "\n\t" #l "\t" d
#include "tbl/experimental_bits.h"
		},
	{ "feature", tweak_feature, NULL,
		NULL, NULL,
		/* default */
		"+validate_headers",
		NULL,
		"Enable/Disable various minor features.\n"
		"\tdefault\tSet default value\n"
		"\tnone\tDisable all features.\n\n"
		"Use +/- prefix to enable/disable individual feature:"
#define FEATURE_BIT(U, l, d) "\n\t" #l "\t" d
#include "tbl/feature_bits.h"
		},
	{ NULL, NULL, NULL }
};
