/*-
 * Copyright (c) 2010-2015 Varnish Software AS
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
 *
 * Parse `import`, check metadata and versioning.
 *
 */

#include "config.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "vcc_compile.h"

#include "libvcc.h"
#include "vfil.h"
#include "vjsn.h"
#include "vmod_abi.h"
#include "vsb.h"

#include "vcc_vmod.h"

struct vmod_import {
	unsigned		magic;
#define VMOD_IMPORT_MAGIC	0x31803a5d
	void			*hdl;
	const char		*err;
	char			*path;

	double			vmod_syntax;
	char			*name;
	char			*func_name;
	char			*file_id;

	struct symbol		*sym;
	const struct token	*t_mod;
	struct vjsn		*vj;
#define STANZA(UU, ll, ss) int n_##ll;
	STANZA_TBL
#undef STANZA
};

typedef void vcc_do_stanza_f(struct vcc *tl, const struct vmod_import *vim,
    const struct vjsn_val *vv);

static int
vcc_path_dlopen(void *priv, const char *fn)
{
	struct vmod_import *vim;

	CAST_OBJ_NOTNULL(vim, priv, VMOD_IMPORT_MAGIC);
	AN(fn);

	vim->hdl = dlopen(fn, RTLD_NOW | RTLD_LOCAL);
	if (vim->hdl == NULL) {
		vim->err = dlerror();
		return (-1);
	}
	return (0);
}

static const char *
vcc_ParseJSON(const struct vcc *tl, const char *jsn, struct vmod_import *vim)
{
	const struct vjsn_val *vv, *vv2, *vv3;
	const char *err;

	vim->vj = vjsn_parse(jsn, &err);
	if (err != NULL)
		return (err);
	AN(vim->vj);

	vv = vim->vj->value;
	if (!vjsn_is_array(vv))
		return ("Not array[0]");

	vv2 = VTAILQ_FIRST(&vv->children);
	if (!vjsn_is_array(vv2))
		return ("Not array[1]");
	vv3 = VTAILQ_FIRST(&vv2->children);
	if (!vjsn_is_string(vv3))
		return ("Not string[2]");
	if (strcmp(vv3->value, "$VMOD"))
		return ("Not $VMOD[3]");

	vv3 = VTAILQ_NEXT(vv3, list);
	assert(vjsn_is_string(vv3));
	vim->vmod_syntax = strtod(vv3->value, NULL);
	assert (vim->vmod_syntax == 1.0);

	vv3 = VTAILQ_NEXT(vv3, list);
	assert(vjsn_is_string(vv3));
	vim->name = vv3->value;

	vv3 = VTAILQ_NEXT(vv3, list);
	assert(vjsn_is_string(vv3));
	vim->func_name = vv3->value;

	vv3 = VTAILQ_NEXT(vv3, list);
	assert(vjsn_is_string(vv3));
	vim->file_id = vv3->value;

	if (!vcc_IdIs(vim->t_mod, vim->name)) {
		VSB_printf(tl->sb, "Wrong file for VMOD %.*s\n",
		    PF(vim->t_mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", vim->path);
		VSB_printf(tl->sb, "\tContains vmod \"%s\"\n", vim->name);
		return ("");
	}

	VTAILQ_FOREACH(vv2, &vv->children, list) {
		assert (vjsn_is_array(vv2));
		vv3 = VTAILQ_FIRST(&vv2->children);
		assert(vjsn_is_string(vv3));
		assert(vv3->value[0] == '$');
#define STANZA(UU, ll, ss) \
    if (!strcmp(vv3->value, "$" #UU)) {vim->n_##ll++; continue;}
		STANZA_TBL
#undef STANZA
		return ("Unknown entry");
	}
	if (vim->n_cproto != 1)
		return ("Bad cproto stanza(s)");
	if (vim->n_vmod != 1)
		return ("Bad vmod stanza(s)");
	return (NULL);
}

/*
 * Load and check the metadata from the objectfile containing the vmod
 */

static int
vcc_VmodLoad(const struct vcc *tl, struct vmod_import *vim, char *fnp)
{
	char buf[256];
	static const char *err;
	const struct vmod_data *vmd;

	CHECK_OBJ_NOTNULL(vim, VMOD_IMPORT_MAGIC);
	bprintf(buf, "Vmod_%.*s_Data", PF(vim->t_mod));
	vmd = dlsym(vim->hdl, buf);
	if (vmd == NULL) {
		VSB_printf(tl->sb, "Malformed VMOD %.*s\n", PF(vim->t_mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_cat(tl->sb, "\t(no Vmod_Data symbol)\n");
		return (-1);
	}
	if (vmd->vrt_major == 0 && vmd->vrt_minor == 0 &&
	    (vmd->abi == NULL || strcmp(vmd->abi, VMOD_ABI_Version))) {
		VSB_printf(tl->sb, "Incompatible VMOD %.*s\n", PF(vim->t_mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_printf(tl->sb, "\tABI mismatch, expected <%s>, got <%s>\n",
			   VMOD_ABI_Version, vmd->abi);
		return (-1);
	}
	if (vmd->vrt_major != 0 &&
	    (vmd->vrt_major != VRT_MAJOR_VERSION ||
	    vmd->vrt_minor > VRT_MINOR_VERSION)) {
		VSB_printf(tl->sb, "Incompatible VMOD %.*s\n", PF(vim->t_mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_printf(tl->sb, "\tVMOD wants ABI version %u.%u\n",
		    vmd->vrt_major, vmd->vrt_minor);
		VSB_printf(tl->sb, "\tvarnishd provides ABI version %u.%u\n",
		    VRT_MAJOR_VERSION, VRT_MINOR_VERSION);
		return (-1);
	}
	if (vmd->name == NULL ||
	    vmd->func == NULL ||
	    vmd->func_len <= 0 ||
	    vmd->json == NULL ||
	    vmd->proto != NULL ||
	    vmd->abi == NULL) {
		VSB_printf(tl->sb, "Mangled VMOD %.*s\n", PF(vim->t_mod));
		VSB_printf(tl->sb, "\tFile name: %s\n", fnp);
		VSB_cat(tl->sb, "\tInconsistent metadata\n");
		return (-1);
	}

	err = vcc_ParseJSON(tl, vmd->json, vim);
	AZ(dlclose(vim->hdl));
	vim->hdl = NULL;
	if (err != NULL && *err != '\0') {
		VSB_printf(tl->sb,
		    "VMOD %.*s: bad metadata\n", PF(vim->t_mod));
		VSB_printf(tl->sb, "\t(%s)\n", err);
		VSB_printf(tl->sb, "\tFile name: %s\n", vim->path);
	}

	if (err != NULL)
		return (-1);

	return(0);
}

static void v_matchproto_(vcc_do_stanza_f)
vcc_do_event(struct vcc *tl, const struct vmod_import *vim,
    const struct vjsn_val *vv)
{
	struct inifin *ifp;

	ifp = New_IniFin(tl);
	VSB_printf(ifp->ini,
	    "\tif (%s(ctx, &vmod_priv_%s, VCL_EVENT_LOAD))\n"
	    "\t\treturn(1);",
	    vv->value, vim->sym->vmod_name);
	VSB_printf(ifp->fin,
	    "\t\t(void)%s(ctx, &vmod_priv_%s,\n"
	    "\t\t\t    VCL_EVENT_DISCARD);",
	    vv->value, vim->sym->vmod_name);
	VSB_printf(ifp->event, "%s(ctx, &vmod_priv_%s, ev)",
	    vv->value, vim->sym->vmod_name);
}

static void v_matchproto_(vcc_do_stanza_f)
vcc_do_cproto(struct vcc *tl, const struct vmod_import *vim,
    const struct vjsn_val *vv)
{
	(void)vim;
	do {
		assert (vjsn_is_string(vv));
		Fh(tl, 0, "%s\n", vv->value);
		vv = VTAILQ_NEXT(vv, list);
	} while(vv != NULL);
}

static void
vcc_vj_foreach(struct vcc *tl, const struct vmod_import *vim,
    const char *stanza, vcc_do_stanza_f *func)
{
	const struct vjsn_val *vv, *vv2, *vv3;

	vv = vim->vj->value;
	assert (vjsn_is_array(vv));
	VTAILQ_FOREACH(vv2, &vv->children, list) {
		assert (vjsn_is_array(vv2));
		vv3 = VTAILQ_FIRST(&vv2->children);
		assert (vjsn_is_string(vv3));
		if (!strcmp(vv3->value, stanza))
			func(tl, vim, VTAILQ_NEXT(vv3, list));
	}
}

static void
vcc_emit_setup(struct vcc *tl, const struct vmod_import *vim)
{
	struct inifin *ifp;
	const struct token *mod = vim->t_mod;

	ifp = New_IniFin(tl);

	VSB_cat(ifp->ini, "\tif (VPI_Vmod_Init(ctx,\n");
	VSB_printf(ifp->ini, "\t    &VGC_vmod_%.*s,\n", PF(mod));
	VSB_printf(ifp->ini, "\t    %u,\n", tl->vmod_count++);
	VSB_printf(ifp->ini, "\t    &%s,\n", vim->func_name);
	VSB_printf(ifp->ini, "\t    sizeof(%s),\n", vim->func_name);
	VSB_printf(ifp->ini, "\t    \"%.*s\",\n", PF(mod));
	VSB_cat(ifp->ini, "\t    ");
	VSB_quote(ifp->ini, vim->path, -1, VSB_QUOTE_CSTR);
	VSB_cat(ifp->ini, ",\n");
	AN(vim->file_id);
	VSB_printf(ifp->ini, "\t    \"%s\",\n", vim->file_id);
	VSB_printf(ifp->ini, "\t    \"./vmod_cache/_vmod_%.*s.%s\"\n",
	    PF(mod), vim->file_id);
	VSB_cat(ifp->ini, "\t    ))\n");
	VSB_cat(ifp->ini, "\t\treturn(1);");

	VSB_cat(tl->symtab, ",\n    {\n");
	VSB_cat(tl->symtab, "\t\"dir\": \"import\",\n");
	VSB_cat(tl->symtab, "\t\"type\": \"$VMOD\",\n");
	VSB_printf(tl->symtab, "\t\"name\": \"%.*s\",\n", PF(mod));
	VSB_printf(tl->symtab, "\t\"file\": \"%s\",\n", vim->path);
	VSB_printf(tl->symtab, "\t\"dst\": \"./vmod_cache/_vmod_%.*s.%s\"\n",
	    PF(mod), vim->file_id);
	VSB_cat(tl->symtab, "    }");

	/* XXX: zero the function pointer structure ?*/
	VSB_printf(ifp->fin, "\t\tVRT_priv_fini(ctx, &vmod_priv_%.*s);",
	    PF(mod));
	VSB_printf(ifp->final, "\t\tVPI_Vmod_Unload(ctx, &VGC_vmod_%.*s);",
	    PF(mod));

	vcc_vj_foreach(tl, vim, "$EVENT", vcc_do_event);

	Fh(tl, 0, "\n/* --- BEGIN VMOD %.*s --- */\n\n", PF(mod));
	Fh(tl, 0, "static struct vmod *VGC_vmod_%.*s;\n", PF(mod));
	Fh(tl, 0, "static struct vmod_priv vmod_priv_%.*s;\n", PF(mod));

	vcc_vj_foreach(tl, vim, "$CPROTO", vcc_do_cproto);

	Fh(tl, 0, "\n/* --- END VMOD %.*s --- */\n\n", PF(mod));
}

static void
vcc_vim_destroy(struct vmod_import **vimp)
{
	struct vmod_import *vim;

	TAKE_OBJ_NOTNULL(vim, vimp, VMOD_IMPORT_MAGIC);
	if (vim->path)
		free(vim->path);
	if (vim->vj)
		vjsn_delete(&vim->vj);
	FREE_OBJ(vim);
}

void
vcc_ParseImport(struct vcc *tl)
{
	char fn[1024];
	const char *p;
	struct token *mod, *tmod, *t1;
	struct symbol *msym, *vsym;
	struct vmod_import *vim;
	const struct vmod_import *vimold;

	t1 = tl->t;
	SkipToken(tl, ID);		/* "import" */

	ExpectErr(tl, ID);		/* "vmod_name" */
	mod = tl->t;
	tmod = vcc_PeekTokenFrom(tl, mod);
	AN(tmod);
	if (tmod->tok == ID && vcc_IdIs(tmod, "as")) {
		vcc_NextToken(tl);		/* "vmod_name" */
		vcc_NextToken(tl);		/* "as" */
		ExpectErr(tl, ID);		/* "vcl_name" */
	}
	tmod = tl->t;

	msym = VCC_SymbolGet(tl, SYM_MAIN, SYM_VMOD, SYMTAB_CREATE, XREF_NONE);
	ERRCHK(tl);
	AN(msym);

	if (tl->t->tok == ID) {
		if (!vcc_IdIs(tl->t, "from")) {
			VSB_cat(tl->sb, "Expected 'from path ...'\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_NextToken(tl);
		if (!tl->unsafe_path && strchr(tl->t->dec, '/')) {
			VSB_cat(tl->sb,
			    "'import ... from path ...' is unsafe.\nAt:");
			vcc_ErrToken(tl, tl->t);
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		ExpectErr(tl, CSTR);
		p = strrchr(tl->t->dec, '/');
		if (p != NULL && p[1] == '\0')
			bprintf(fn, "%slibvmod_%.*s.so", tl->t->dec, PF(mod));
		else
			bprintf(fn, "%s", tl->t->dec);
		vcc_NextToken(tl);
	} else {
		bprintf(fn, "libvmod_%.*s.so", PF(mod));
	}

	SkipToken(tl, ';');

	ALLOC_OBJ(vim, VMOD_IMPORT_MAGIC);
	AN(vim);
	vim->t_mod = mod;
	vim->sym = msym;

	if (VFIL_searchpath(tl->vmod_path, vcc_path_dlopen, vim, fn, &vim->path)) {
		if (vim->err == NULL) {
			VSB_printf(tl->sb,
			    "Could not find VMOD %.*s\n", PF(mod));
		} else {
			VSB_printf(tl->sb,
			    "Could not open VMOD %.*s\n", PF(mod));
			VSB_printf(tl->sb, "\tFile name: %s\n",
			    vim->path != NULL ? vim->path : fn);
			VSB_printf(tl->sb, "\tdlerror: %s\n", vim->err);
		}
		vcc_ErrWhere(tl, mod);
		vcc_vim_destroy(&vim);
		return;
	}

	if (vcc_VmodLoad(tl, vim, vim->path) < 0 || tl->err) {
		vcc_ErrWhere(tl, vim->t_mod);
		vcc_vim_destroy(&vim);
		return;
	}

	vimold = msym->import;
	if (vimold != NULL) {
		CHECK_OBJ_NOTNULL(vimold, VMOD_IMPORT_MAGIC);
		if (!strcmp(vimold->file_id, vim->file_id)) {
			/* Identical import is OK */
		} else {
			VSB_printf(tl->sb,
			    "Another module already imported as %.*s.\n",
			    PF(tmod));
			vcc_ErrWhere2(tl, t1, tl->t);
		}
		vcc_vim_destroy(&vim);
		return;
	}
	msym->def_b = t1;
	msym->def_e = tl->t;

	VTAILQ_FOREACH(vsym, &tl->sym_vmods, sideways) {
		assert(vsym->kind == SYM_VMOD);
		vimold = vsym->import;
		CHECK_OBJ_NOTNULL(vimold, VMOD_IMPORT_MAGIC);
		if (!strcmp(vimold->file_id, vim->file_id)) {
			/* Already loaded under different name */
			msym->eval_priv = vsym->eval_priv;
			msym->import = vsym->import;
			msym->vmod_name = vsym->vmod_name;
			vcc_VmodSymbols(tl, msym);
			// XXX: insert msym in sideways ?
			vcc_vim_destroy(&vim);
			return;
		}
	}

	VTAILQ_INSERT_TAIL(&tl->sym_vmods, msym, sideways);

	msym->eval_priv = vim->vj;
	msym->import = vim;
	msym->vmod_name = TlDup(tl, vim->name);
	vcc_VmodSymbols(tl, msym);

	vcc_emit_setup(tl, vim);
}
