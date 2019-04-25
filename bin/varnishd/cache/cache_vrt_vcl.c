/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2016 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include "cache_varnishd.h"

#include "vcl.h"
#include "vtim.h"

#include "vcc_interface.h"

#include "cache_director.h"
#include "cache_vcl.h"

/*--------------------------------------------------------------------*/

const char *
VCL_Return_Name(unsigned r)
{

	switch (r) {
#define VCL_RET_MAC(l, U, B)	\
	case VCL_RET_##U:	\
		return(#l);
#include "tbl/vcl_returns.h"
	default:
		return (NULL);
	}
}

const char *
VCL_Method_Name(unsigned m)
{

	switch (m) {
#define VCL_MET_MAC(func, upper, typ, bitmap)	\
	case VCL_MET_##upper:			\
		return (#upper);
#include "tbl/vcl_returns.h"
	default:
		return (NULL);
	}
}

/*--------------------------------------------------------------------*/

void
VCL_Refresh(struct vcl **vcc)
{

	while (vcl_active == NULL)
		(void)usleep(100000);

	if (*vcc == vcl_active)
		return;
	if (*vcc != NULL)
		VCL_Rel(vcc);	/* XXX: optimize locking */

	vcl_get(vcc, NULL);
}

void
VCL_Recache(struct worker *wrk, struct vcl **vclp)
{

	AN(wrk);
	AN(vclp);
	CHECK_OBJ_NOTNULL(*vclp, VCL_MAGIC);

	if (*vclp != vcl_active || wrk->vcl == vcl_active) {
		VCL_Rel(vclp);
		return;
	}
	if (wrk->vcl != NULL)
		VCL_Rel(&wrk->vcl);
	wrk->vcl = *vclp;
	*vclp = NULL;
}

void
VCL_Ref(struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	AZ(errno=pthread_rwlock_rdlock(&vcl->temp_rwl));
	assert(!VCL_COLD(vcl));
	AZ(errno=pthread_rwlock_unlock(&vcl->temp_rwl));
	Lck_Lock(&vcl_mtx);
	assert(vcl->busy > 0);
	vcl->busy++;
	Lck_Unlock(&vcl_mtx);
}

void
VCL_Rel(struct vcl **vcc)
{
	struct vcl *vcl;

	AN(*vcc);
	vcl = *vcc;
	*vcc = NULL;

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	Lck_Lock(&vcl_mtx);
	assert(vcl->busy > 0);
	vcl->busy--;
	/*
	 * We do not garbage collect discarded VCL's here, that happens
	 * in VCL_Poll() which is called from the CLI thread.
	 */
	Lck_Unlock(&vcl_mtx);
}

/*--------------------------------------------------------------------*/

void
VRT_VCL_Busy(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);

	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);
	VCL_Ref(ctx->vcl);
}

void
VRT_VCL_Unbusy(VRT_CTX)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	struct vcl *vcl;

	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);
	vcl = ctx->vcl;
	VCL_Rel(&vcl);
}

/*--------------------------------------------------------------------*/

VCL_BACKEND
VRT_AddDirector(VRT_CTX, const struct vdi_methods *m, void *priv,
    const char *fmt, ...)
{
	struct vsb *vsb;
	struct vcl *vcl;
	struct vcldir *vdir;
	struct director *d;
	va_list ap;
	int i;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(m, VDI_METHODS_MAGIC);
	AN(fmt);
	vcl = ctx->vcl;
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	AZ(errno=pthread_rwlock_rdlock(&vcl->temp_rwl));
	if (vcl->temp == VCL_TEMP_COOLING) {
		AZ(errno=pthread_rwlock_unlock(&vcl->temp_rwl));
		return (NULL);
	}

	ALLOC_OBJ(d, DIRECTOR_MAGIC);
	AN(d);
	ALLOC_OBJ(vdir, VCLDIR_MAGIC);
	AN(vdir);
	vdir->dir = d;
	d->vdir = vdir;

	vdir->methods = m;
	d->priv = priv;
	vsb = VSB_new_auto();
	AN(vsb);
	VSB_printf(vsb, "%s.", VCL_Name(vcl));
	i = VSB_len(vsb);
	va_start(ap, fmt);
	VSB_vprintf(vsb, fmt, ap);
	va_end(ap);
	AZ(VSB_finish(vsb));
	REPLACE(vdir->cli_name, VSB_data(vsb));
	VSB_destroy(&vsb);
	d->vcl_name = vdir->cli_name + i;

	vdir->vcl = vcl;
	vdir->admin_health = VDI_AH_AUTO;
	vdir->health_changed = VTIM_real();

	Lck_Lock(&vcl_mtx);
	VTAILQ_INSERT_TAIL(&vcl->director_list, vdir, list);
	Lck_Unlock(&vcl_mtx);

	if (VCL_WARM(vcl))
		/* Only when adding backend to already warm VCL */
		VDI_Event(d, VCL_EVENT_WARM);
	else if (vcl->temp != VCL_TEMP_INIT)
		WRONG("Dynamic Backends can only be added to warm VCLs");
	AZ(errno=pthread_rwlock_unlock(&vcl->temp_rwl));

	return (d);
}

void
VRT_DelDirector(VCL_BACKEND *bp)
{
	struct vcl *vcl;
	struct vcldir *vdir;
	VCL_BACKEND d;

	TAKE_OBJ_NOTNULL(d, bp, DIRECTOR_MAGIC);
	vdir = d->vdir;
	CHECK_OBJ_NOTNULL(vdir, VCLDIR_MAGIC);
	vcl = vdir->vcl;
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	Lck_Lock(&vcl_mtx);
	VTAILQ_REMOVE(&vcl->director_list, vdir, list);
	Lck_Unlock(&vcl_mtx);

	AZ(errno=pthread_rwlock_rdlock(&vcl->temp_rwl));
	if (VCL_WARM(vcl))
		VDI_Event(d, VCL_EVENT_COLD);
	AZ(errno=pthread_rwlock_unlock(&vcl->temp_rwl));
	if(vdir->methods->destroy != NULL)
		vdir->methods->destroy(d);
	free(vdir->cli_name);
	FREE_OBJ(vdir->dir);
	FREE_OBJ(vdir);
}

void
VRT_DisableDirector(VCL_BACKEND d)
{
	struct vcldir *vdir;

	CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
	vdir = d->vdir;
	CHECK_OBJ_NOTNULL(vdir, VCLDIR_MAGIC);

	vdir->admin_health = VDI_AH_DELETED;
	vdir->health_changed = VTIM_real();
}

VCL_BACKEND
VRT_LookupDirector(VRT_CTX, VCL_STRING name)
{
	struct vcl *vcl;
	struct vcldir *vdir;
	VCL_BACKEND dd, d = NULL;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(name);

	assert(ctx->method & VCL_MET_TASK_H);
	ASSERT_CLI();

	vcl = ctx->vcl;
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);

	Lck_Lock(&vcl_mtx);
	VTAILQ_FOREACH(vdir, &vcl->director_list, list) {
		dd = vdir->dir;
		if (strcmp(dd->vcl_name, name))
			continue;
		d = dd;
		break;
	}
	Lck_Unlock(&vcl_mtx);

	return (d);
}

/*--------------------------------------------------------------------*/

VCL_BACKEND
VRT_DirectorResolve(VRT_CTX, VCL_BACKEND d)
{
	VCL_BACKEND d2;

	for (; d != NULL && d->vdir->methods->resolve != NULL; d = d2) {
		CHECK_OBJ_NOTNULL(d, DIRECTOR_MAGIC);
		AN(d->vdir);
		d2 = d->vdir->methods->resolve(ctx, d);
	}
	CHECK_OBJ_ORNULL(d, DIRECTOR_MAGIC);
	if (d != NULL)
		AN(d->vdir);
	return (d);
}

/*--------------------------------------------------------------------*/

VCL_BACKEND
VCL_DefaultDirector(const struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);
	return (*vcl->conf->default_director);
}

const char *
VCL_Name(const struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	return (vcl->loaded_name);
}

VCL_PROBE
VCL_DefaultProbe(const struct vcl *vcl)
{

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);
	return (vcl->conf->default_probe);
}

/*--------------------------------------------------------------------
 * VRT apis relating to VCL's as VCLS.
 */

void
VRT_count(VRT_CTX, unsigned u)
{

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl, VCL_MAGIC);
	CHECK_OBJ_NOTNULL(ctx->vcl->conf, VCL_CONF_MAGIC);
	assert(u < ctx->vcl->conf->nref);
	if (ctx->vsl != NULL)
		VSLb(ctx->vsl, SLT_VCL_trace, "%s %u %u.%u.%u",
		    ctx->vcl->loaded_name, u, ctx->vcl->conf->ref[u].source,
		    ctx->vcl->conf->ref[u].line, ctx->vcl->conf->ref[u].pos);
	else
		VSL(SLT_VCL_trace, 0, "%s %u %u.%u.%u",
		    ctx->vcl->loaded_name, u, ctx->vcl->conf->ref[u].source,
		    ctx->vcl->conf->ref[u].line, ctx->vcl->conf->ref[u].pos);
}

VCL_VCL
VPI_vcl_get(VRT_CTX, const char *name)
{
	VCL_VCL vcl;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	vcl = vcl_find(name);
	AN(vcl);
	Lck_Lock(&vcl_mtx);
	vcl->nrefs++;
	Lck_Unlock(&vcl_mtx);
	return (vcl);
}

void
VPI_vcl_rel(VRT_CTX, VCL_VCL vcl)
{
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(vcl);
	Lck_Lock(&vcl_mtx);
	vcl->nrefs--;
	Lck_Unlock(&vcl_mtx);
}

void
VPI_vcl_select(VRT_CTX, VCL_VCL vcl)
{
	struct req *req = ctx->req;

	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);

	if (IS_TOPREQ(req) && req->vcl0 != NULL)
		return;		// Illega, req-FSM will fail this later.

	VCL_TaskLeave(req->vcl, req->privs);
	if (IS_TOPREQ(req)) {
		req->vcl0 = req->vcl;
		req->vcl = NULL;
	} else {
		VCL_Rel(&req->vcl);
	}
	vcl_get(&req->vcl, vcl);
	VSLb(ctx->req->vsl, SLT_VCL_use, "%s via %s",
	    req->vcl->loaded_name, vcl->loaded_name);
	VCL_TaskEnter(req->vcl, req->privs);
}

struct vclref *
VRT_ref_vcl(VRT_CTX, const char *desc)
{
	struct vcl *vcl;
	struct vclref* ref;

	ASSERT_CLI();
	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	AN(desc);
	AN(*desc);

	vcl = ctx->vcl;
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	assert(VCL_WARM(vcl));

	ALLOC_OBJ(ref, VCLREF_MAGIC);
	AN(ref);
	ref->vcl = vcl;
	bprintf(ref->desc, "%s", desc);

	Lck_Lock(&vcl_mtx);
	VTAILQ_INSERT_TAIL(&vcl->ref_list, ref, list);
	vcl->nrefs++;
	Lck_Unlock(&vcl_mtx);

	return (ref);
}

void
VRT_rel_vcl(VRT_CTX, struct vclref **refp)
{
	struct vcl *vcl;
	struct vclref *ref;

	AN(refp);
	ref = *refp;
	*refp = NULL;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(ref, VCLREF_MAGIC);

	vcl = ctx->vcl;
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);
	assert(vcl == ref->vcl);

	/* NB: A VCL may be released by a VMOD at any time, but it must happen
	 * after a warmup and before the end of a cooldown. The release may or
	 * may not happen while the same thread holds the temperature lock, so
	 * instead we check that all references are gone in VCL_Nuke.
	 */

	Lck_Lock(&vcl_mtx);
	assert(!VTAILQ_EMPTY(&vcl->ref_list));
	VTAILQ_REMOVE(&vcl->ref_list, ref, list);
	vcl->nrefs--;
	/* No garbage collection here, for the same reasons as in VCL_Rel. */
	Lck_Unlock(&vcl_mtx);

	FREE_OBJ(ref);
}

/*--------------------------------------------------------------------
 * Method functions to call into VCL programs.
 *
 * Either the request or busyobject must be specified, but not both.
 * The workspace argument is where random VCL stuff gets space from.
 */

static void
vcl_call_method(struct worker *wrk, struct req *req, struct busyobj *bo,
    void *specific, unsigned method, vcl_func_f *func)
{
	uintptr_t aws;
	struct vrt_ctx ctx;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	INIT_OBJ(&ctx, VRT_CTX_MAGIC);
	if (req != NULL) {
		CHECK_OBJ(req, REQ_MAGIC);
		CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);
		CHECK_OBJ_NOTNULL(req->vcl, VCL_MAGIC);
		VCL_Req2Ctx(&ctx, req);
	}
	if (bo != NULL) {
		if (req)
			assert(method == VCL_MET_PIPE);
		CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);
		CHECK_OBJ_NOTNULL(bo->vcl, VCL_MAGIC);
		VCL_Bo2Ctx(&ctx, bo);
	}
	assert(ctx.now != 0);
	ctx.syntax = ctx.vcl->conf->syntax;
	ctx.specific = specific;
	ctx.method = method;
	wrk->handling = 0;
	ctx.handling = &wrk->handling;
	aws = WS_Snapshot(wrk->aws);
	wrk->cur_method = method;
	wrk->seen_methods |= method;
	AN(ctx.vsl);
	VSLb(ctx.vsl, SLT_VCL_call, "%s", VCL_Method_Name(method));
	func(&ctx);
	VSLb(ctx.vsl, SLT_VCL_return, "%s", VCL_Return_Name(wrk->handling));
	wrk->cur_method |= 1;		// Magic marker
	if (wrk->handling == VCL_RET_FAIL)
		wrk->stats->vcl_fail++;

	/*
	 * VCL/Vmods are not allowed to make permanent allocations from
	 * wrk->aws, but they can reserve and return from it.
	 */
	assert(aws == WS_Snapshot(wrk->aws));
}

#define VCL_MET_MAC(func, upper, typ, bitmap)				\
void									\
VCL_##func##_method(struct vcl *vcl, struct worker *wrk,		\
     struct req *req, struct busyobj *bo, void *specific)		\
{									\
									\
	CHECK_OBJ_NOTNULL(vcl, VCL_MAGIC);				\
	CHECK_OBJ_NOTNULL(vcl->conf, VCL_CONF_MAGIC);			\
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);				\
	vcl_call_method(wrk, req, bo, specific,				\
	    VCL_MET_ ## upper, vcl->conf->func##_func);			\
	AN((1U << wrk->handling) & bitmap);				\
}

#include "tbl/vcl_returns.h"
