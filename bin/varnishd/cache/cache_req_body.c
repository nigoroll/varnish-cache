/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 */

#include "config.h"

#include <stdlib.h>

#include "cache_varnishd.h"
#include "cache_filter.h"
#include "cache_objhead.h"
#include "cache_transport.h"

#include "vtim.h"
#include "storage/storage.h"
#include "hash/hash_slinger.h"

/*----------------------------------------------------------------------
 * Check and potentially update req framing headers.
 */

static ssize_t
vrb_cached(struct req *req, ssize_t req_bodybytes)
{
	ssize_t l0, l;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(req->body_oc);
	assert(req_bodybytes >= 0);

	l0 = http_GetContentLength(req->http0);
	l = http_GetContentLength(req->http);

	assert(req->req_body_status->avail > 0);
	if (req->req_body_status->length_known) {
		if (req->req_body_partial) {
			assert(req_bodybytes < l0);
			assert(req_bodybytes < l);
		} else {
			assert(req_bodybytes == l0);
			assert(req_bodybytes == l);
		}
		AZ(http_GetHdr(req->http0, H_Transfer_Encoding, NULL));
		AZ(http_GetHdr(req->http, H_Transfer_Encoding, NULL));
	} else if (!req->req_body_partial) {
		/* We must update also the "pristine" req.* copy */
		AZ(http_GetHdr(req->http0, H_Content_Length, NULL));
		assert(l0 < 0);
		http_Unset(req->http0, H_Transfer_Encoding);
		http_PrintfHeader(req->http0, "Content-Length: %ju",
		    (uintmax_t)req_bodybytes);

		AZ(http_GetHdr(req->http, H_Content_Length, NULL));
		assert(l < 0);
		http_Unset(req->http, H_Transfer_Encoding);
		http_PrintfHeader(req->http, "Content-Length: %ju",
		    (uintmax_t)req_bodybytes);
	}

	return (req_bodybytes);
}

/*----------------------------------------------------------------------
 * Pull the req.body in via/into a objcore
 *
 * called once for caching with func == NULL
 * called once without caching with func != NULL
 * called twice for partial caching:
 * first call with func == NULL second with func != NULL
 *
 */

static ssize_t
vrb_pull(struct req *req, ssize_t maxsize, unsigned partial,
    objiterate_f *func, void *priv)
{
	ssize_t l, r = 0, yet;
	struct vrt_ctx ctx[1];
	struct vfp_ctx *vfc;
	uint8_t *ptr;
	enum vfp_status vfps = VFP_ERROR;
	const struct stevedore *stv;
	struct objcore *oc;
	ssize_t req_bodybytes = 0;
	uint64_t oa_len;
	char c;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	CHECK_OBJ_NOTNULL(req->htc, HTTP_CONN_MAGIC);
	CHECK_OBJ_NOTNULL(req->vfc, VFP_CTX_MAGIC);
	vfc = req->vfc;
	AN(maxsize);

	if (req->req_body_partial) {
		assert(req->req_body_status == BS_TAKEN);
		AN(req->body_oc);
		AZ(ObjGetU64(req->wrk, req->body_oc, OA_LEN, &oa_len));
		AN(oa_len);
		req_bodybytes = oa_len;
	}

	oc = HSH_Private(req->wrk);
	AN(oc);

	if (req->storage != NULL)
		stv = req->storage;
	else
		stv = stv_transient;

	if (STV_NewObject(req->wrk, oc, stv, 8) == 0) {
		req->req_body_status = BS_ERROR;
		HSH_DerefBoc(req->wrk, oc);
		AZ(HSH_DerefObjCore(req->wrk, &oc, 0));
		(void)VFP_Error(vfc, "Object allocation failed:"
		    " Ran out of space in %s", stv->vclname);
		return (-1);
	}

	vfc->oc = oc;

	/* NB: we need to open the VFP exactly once, even though we may work
	 * on up to two pairs of objcore/boc for the same request body when
	 * partial caching is involved.
	 */
	if (req_bodybytes == 0) {
		INIT_OBJ(ctx, VRT_CTX_MAGIC);
		VCL_Req2Ctx(ctx, req);

		if (VFP_Open(ctx, vfc) < 0) {
			req->req_body_status = BS_ERROR;
			HSH_DerefBoc(req->wrk, oc);
			AZ(HSH_DerefObjCore(req->wrk, &oc, 0));
			return (-1);
		}
	}

	assert(vfc->wrk == req->wrk);
	AZ(vfc->failed);
	AN(req->htc);
	yet = req->htc->content_length;
	if (yet != 0 && req->want100cont) {
		req->want100cont = 0;
		(void)req->transport->minimal_response(req, 100);
	}
	yet = vmax_t(ssize_t, yet, 0);
	do {
		AZ(vfc->failed);
		l = (maxsize < 0) ? yet : maxsize - req_bodybytes;
		if (VFP_GetStorage(vfc, &l, &ptr) != VFP_OK)
			break;
		AZ(vfc->failed);
		AN(ptr);
		AN(l);
		vfps = VFP_Suck(vfc, ptr, &l);
		if (l > 0 && vfps != VFP_ERROR) {
			req_bodybytes += l;
			if (yet >= l)
				yet -= l;
			if (func != NULL) {
				r = func(priv, 1, ptr, l);
				if (r)
					break;
			} else {
				ObjExtend(req->wrk, oc, l,
				    vfps == VFP_END ? 1 : 0);
			}
		}

	} while (vfps == VFP_OK && (maxsize < 0 || req_bodybytes < maxsize));

	AZ(ObjSetU64(req->wrk, oc, OA_LEN, req_bodybytes));
	HSH_DerefBoc(req->wrk, oc);

	if (!partial) {
		/* VFP_END means that the body fit into the given size, but we
		 * might need one extra read to see it if a chunked encoding
		 * zero chunk is delayed
		 */
		if (vfps == VFP_OK) {
			l = 1;
			vfps = VFP_Suck(vfc, &c, &l);
		}
		if (vfps == VFP_OK)
			vfps = VFP_Error(vfc, "Request body too big to cache");
	}

	if (r)
		vfps = VFP_Error(vfc, "Iterator failed");

	switch (vfps) {
	case VFP_ERROR:
		req->req_body_status = BS_ERROR;
		if (r == 0)
			r = -1;
		/* FALLTHROUGH */
	case VFP_END:
		req->acct.req_bodybytes += VFP_Close(vfc);
		VSLb_ts_req(req, "ReqBody", VTIM_real());
		break;
	default:
		break;
	}

	if (func != NULL || vfps == VFP_ERROR) {
		/* no caching or error */
		AZ(HSH_DerefObjCore(req->wrk, &oc, 0));
		return (r);
	}

	/* caching, can not cache twice, reference kept */
	AZ(req->body_oc);

	switch (vfps) {
	case VFP_OK:
		AN(partial);
		req->req_body_partial = 1;
		/* FALLTHROUGH */
	case VFP_END:
		req->body_oc = oc;
		return (vrb_cached(req, req_bodybytes));
	default:
		WRONG("vfp status");
	}
}

/*----------------------------------------------------------------------
 * Iterate over the req.body.
 *
 * This can be done exactly once if uncached, and multiple times if the
 * req.body is cached.
 *
 * return length or -1 on error
 */

static int v_matchproto_(objiterate_f)
httpq_req_body_discard(void *priv, unsigned flush, const void *ptr, ssize_t len)
{

	(void)priv;
	(void)flush;
	(void)ptr;
	(void)len;
	return (0);
}

ssize_t
VRB_Iterate(struct worker *wrk, struct vsl_log *vsl,
    struct req *req, objiterate_f *func, void *priv, enum vrb_what_e what)
{
	int i;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	AN(vsl);
	AN(func);

	if (func == httpq_req_body_discard)
		assert (what == VRB_ALL);

	if (req->body_oc && !req->req_body_partial) {
		if (what == VRB_REMAIN)
			return (0);

		if (ObjIterate(wrk, req->body_oc, priv, func, 0))
			return (-1);
		return (0);
	}
	if (req->req_body_status == BS_NONE)
		return (0);
	if (req->req_body_status == BS_ERROR) {
		VSLb(vsl, SLT_FetchError,
		    "Had failed reading req.body before.");
		return (-1);
	}

	if (req->req_body_partial && what != VRB_REMAIN) {
		if (ObjIterate(wrk, req->body_oc, priv, func, 0)) {
			VSLb(vsl, SLT_FetchError,
			    "Failed to send a partial req.body");
			return (-1);
		}
	}
	if (what == VRB_CACHED)
		return (0);
	if (req->req_body_status == BS_TAKEN) {
		VSLb(vsl, SLT_VCL_Error,
		    "Uncached req.body can only be consumed once.");
		return (-1);
	}
	Lck_Lock(&req->sp->mtx);
	if (req->req_body_status->avail > 0) {
		req->req_body_status = BS_TAKEN;
		i = 0;
	} else
		i = -1;
	Lck_Unlock(&req->sp->mtx);
	if (i) {
		VSLb(vsl, SLT_VCL_Error,
		    "Multiple attempts to access non-cached req.body");
		return (i);
	}
	return (vrb_pull(req, -1, 0, func, priv));
}

/*----------------------------------------------------------------------
 * VRB_Ignore() is a dedicated function, because we might
 * be able to disuade or terminate its transmission in some protocols.
 *
 * For HTTP1, we do nothing if we are going to close the connection anyway or
 * just iterate it into oblivion.
 */

int
VRB_Ignore(struct req *req)
{

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	if (req->doclose != SC_NULL)
		return (0);
	if (req->req_body_status->avail > 0)
		(void)VRB_Iterate(req->wrk, req->vsl, req,
		    httpq_req_body_discard, NULL, VRB_ALL);
	if (req->req_body_status == BS_ERROR)
		req->doclose = SC_RX_BODY;
	return (0);
}

/*----------------------------------------------------------------------
 */

void
VRB_Deref(struct req *req)
{
	int r;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);

	if (req->body_oc == NULL) {
		AZ(req->req_body_partial);
		return;
	}

	r = HSH_DerefObjCore(req->wrk, &req->body_oc, 0);

	// each busyobj may have gained a reference
	assert (r >= 0);
	assert ((unsigned)r <= req->restarts + 1);

	if (r != 0)
		return;

	req->req_body_partial = 0;
}

/*----------------------------------------------------------------------
 * Cache the req.body if it is smaller than the given size
 *
 * This function must be called before any backend fetches are kicked
 * off to prevent parallelism.
 */

ssize_t
VRB_Cache(struct req *req, ssize_t maxsize, unsigned partial)
{
	uint64_t u;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	assert (req->req_step == R_STP_RECV);

	if (maxsize <= 0) {
		VSLb(req->vsl, SLT_VCL_Error, "Cannot cache an empty req.body");
		return (-1);
	}

	/*
	 * We only allow caching to happen the first time through vcl_recv{}
	 * where we know we will have no competition or conflicts for the
	 * updates to req.http.* etc.
	 */
	if (req->restarts > 0 && req->body_oc == NULL) {
		VSLb(req->vsl, SLT_VCL_Error,
		    "req.body must be cached before restarts");
		return (-1);
	}

	if (req->body_oc) {
		AZ(ObjGetU64(req->wrk, req->body_oc, OA_LEN, &u));
		return (u);
	}

	if (req->req_body_status->avail <= 0)
		return (req->req_body_status->avail);

	if (req->htc->content_length > maxsize && !partial) {
		req->req_body_status = BS_ERROR;
		(void)VFP_Error(req->vfc, "Request body too big to cache");
		return (-1);
	}

	return (vrb_pull(req, maxsize, partial, NULL, NULL));
}
