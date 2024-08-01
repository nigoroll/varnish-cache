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
 *
 * Write data to fd
 * We try to use writev() if possible in order to minimize number of
 * syscalls made and packets sent.  It also just might allow the worker
 * thread to complete the request without holding stuff locked.
 *
 * XXX: chunked header (generated in Flush) and Tail (EndChunk)
 *      are not accounted by means of the size_t returned. Obvious ideas:
 *	- add size_t return value to Flush and EndChunk
 *	- base accounting on (struct v1l).cnt
 */

#include "config.h"

#include <sys/uio.h>
#include "cache/cache_varnishd.h"
#include "cache/cache_filter.h"

#include <stdio.h>

#include "cache_http1.h"
#include "vtim.h"

/*--------------------------------------------------------------------*/

struct v1l {
	unsigned		magic;
#define V1L_MAGIC		0x2f2142e5
	int			*wfd;
	stream_close_t		werr;	/* valid after V1L_Flush() */
	struct iovec		*iov;
	unsigned		siov;
	unsigned		niov;
	ssize_t			liov;
	ssize_t			cliov;
	unsigned		ciov;	/* Chunked header marker */
	vtim_real		deadline;
	struct vsl_log		*vsl;
	ssize_t			cnt;	/* Flushed byte count */
	struct ws		*ws;
	uintptr_t		ws_snap;
};

/*--------------------------------------------------------------------
 * for niov == 0, reserve the ws for max number of iovs
 * otherwise, up to niov
 */

void
V1L_Open(struct worker *wrk, struct ws *ws, int *fd, struct vsl_log *vsl,
    vtim_real deadline, unsigned niov)
{
	struct v1l *v1l;
	unsigned u;
	uintptr_t ws_snap;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AZ(wrk->v1l);

	if (WS_Overflowed(ws))
		return;

	if (niov != 0)
		assert(niov >= 3);

	ws_snap = WS_Snapshot(ws);

	v1l = WS_Alloc(ws, sizeof *v1l);
	if (v1l == NULL)
		return;
	INIT_OBJ(v1l, V1L_MAGIC);

	v1l->ws = ws;
	v1l->ws_snap = ws_snap;

	u = WS_ReserveLumps(ws, sizeof(struct iovec));
	if (u < 3) {
		/* Must have at least 3 in case of chunked encoding */
		WS_Release(ws, 0);
		WS_MarkOverflow(ws);
		return;
	}
	if (u > IOV_MAX)
		u = IOV_MAX;
	if (niov != 0 && u > niov)
		u = niov;
	v1l->iov = WS_Reservation(ws);
	v1l->siov = u;
	v1l->ciov = u;
	v1l->wfd = fd;
	v1l->deadline = deadline;
	v1l->vsl = vsl;
	v1l->werr = SC_NULL;

	AZ(wrk->v1l);
	wrk->v1l = v1l;

	WS_Release(ws, u * sizeof(struct iovec));
}

stream_close_t
V1L_Close(struct worker *wrk, uint64_t *cnt)
{
	struct v1l *v1l;
	struct ws *ws;
	uintptr_t ws_snap;
	stream_close_t sc;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	AN(cnt);
	sc = V1L_Flush(wrk);
	TAKE_OBJ_NOTNULL(v1l, &wrk->v1l, V1L_MAGIC);
	*cnt += v1l->cnt;
	ws = v1l->ws;
	ws_snap = v1l->ws_snap;
	ZERO_OBJ(v1l, sizeof *v1l);
	WS_Rollback(ws, ws_snap);
	return (sc);
}

/* change the number of iovs */
stream_close_t
V1L_Reopen(struct worker *wrk, uint64_t *cnt, unsigned niov)
{
	struct v1l *v1l = wrk->v1l;

	stream_close_t sc;
	struct ws *ws;
	int *fd;
	struct vsl_log *vsl;
	vtim_real deadline;

	ws = v1l->ws;
	fd = v1l->wfd;
	vsl = v1l->vsl;
	deadline = v1l->deadline;
	v1l = NULL;

	sc = V1L_Close(wrk, cnt);
	if (sc != SC_NULL)
		return (sc);

	V1L_Open(wrk, ws, fd, vsl, deadline, niov);
	return (sc);
}


static void
v1l_prune(struct v1l *v1l, size_t bytes)
{
	ssize_t used = 0;
	ssize_t j, used_here;

	for (j = 0; j < v1l->niov; j++) {
		if (used + v1l->iov[j].iov_len > bytes) {
			/* Cutoff is in this iov */
			used_here = bytes - used;
			v1l->iov[j].iov_len -= used_here;
			v1l->iov[j].iov_base =
			    (char*)v1l->iov[j].iov_base + used_here;
			memmove(v1l->iov, &v1l->iov[j],
			    (v1l->niov - j) * sizeof(struct iovec));
			v1l->niov -= j;
			v1l->liov -= bytes;
			return;
		}
		used += v1l->iov[j].iov_len;
	}
	AZ(v1l->liov);
}

stream_close_t
V1L_Flush(const struct worker *wrk)
{
	ssize_t i;
	int err;
	struct v1l *v1l;
	char cbuf[32];

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	v1l = wrk->v1l;
	CHECK_OBJ_NOTNULL(v1l, V1L_MAGIC);
	CHECK_OBJ_NOTNULL(v1l->werr, STREAM_CLOSE_MAGIC);
	AN(v1l->wfd);

	assert(v1l->niov <= v1l->siov);

	if (*v1l->wfd >= 0 && v1l->liov > 0 && v1l->werr == SC_NULL) {
		if (v1l->ciov < v1l->siov && v1l->cliov > 0) {
			/* Add chunk head & tail */
			bprintf(cbuf, "00%zx\r\n", v1l->cliov);
			i = strlen(cbuf);
			v1l->iov[v1l->ciov].iov_base = cbuf;
			v1l->iov[v1l->ciov].iov_len = i;
			v1l->liov += i;

			/* This is OK, because siov was --'ed */
			v1l->iov[v1l->niov].iov_base = cbuf + i - 2;
			v1l->iov[v1l->niov++].iov_len = 2;
			v1l->liov += 2;
		} else if (v1l->ciov < v1l->siov) {
			v1l->iov[v1l->ciov].iov_base = cbuf;
			v1l->iov[v1l->ciov].iov_len = 0;
		}

		i = 0;
		err = 0;
		do {
			if (VTIM_real() > v1l->deadline) {
				VSLb(v1l->vsl, SLT_Debug,
				    "Hit total send timeout, "
				    "wrote = %zd/%zd; not retrying",
				    i, v1l->liov);
				i = -1;
				break;
			}

			i = writev(*v1l->wfd, v1l->iov, v1l->niov);
			if (i > 0)
				v1l->cnt += i;

			if (i == v1l->liov)
				break;

			/* we hit a timeout, and some data may have been sent:
			 * Remove sent data from start of I/O vector, then retry
			 *
			 * XXX: Add a "minimum sent data per timeout counter to
			 * prevent slowloris attacks
			 */

			err = errno;

			if (err == EWOULDBLOCK) {
				VSLb(v1l->vsl, SLT_Debug,
				    "Hit idle send timeout, "
				    "wrote = %zd/%zd; retrying",
				    i, v1l->liov);
			}

			if (i > 0)
				v1l_prune(v1l, i);
		} while (i > 0 || err == EWOULDBLOCK);

		if (i <= 0) {
			VSLb(v1l->vsl, SLT_Debug,
			    "Write error, retval = %zd, len = %zd, errno = %s",
			    i, v1l->liov, VAS_errtxt(err));
			assert(v1l->werr == SC_NULL);
			if (err == EPIPE)
				v1l->werr = SC_REM_CLOSE;
			else
				v1l->werr = SC_TX_ERROR;
			errno = err;
		}
	}
	v1l->liov = 0;
	v1l->cliov = 0;
	v1l->niov = 0;
	if (v1l->ciov < v1l->siov)
		v1l->ciov = v1l->niov++;
	CHECK_OBJ_NOTNULL(v1l->werr, STREAM_CLOSE_MAGIC);
	return (v1l->werr);
}

size_t
V1L_Write(const struct worker *wrk, const void *ptr, ssize_t len)
{
	struct v1l *v1l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	v1l = wrk->v1l;
	CHECK_OBJ_NOTNULL(v1l, V1L_MAGIC);
	AN(v1l->wfd);
	if (len == 0 || *v1l->wfd < 0)
		return (0);
	if (len == -1)
		len = strlen(ptr);
	assert(v1l->niov < v1l->siov);
	v1l->iov[v1l->niov].iov_base = TRUST_ME(ptr);
	v1l->iov[v1l->niov].iov_len = len;
	v1l->liov += len;
	v1l->niov++;
	v1l->cliov += len;
	if (v1l->niov >= v1l->siov) {
		(void)V1L_Flush(wrk);
		VSC_C_main->http1_iovs_flush++;
	}
	return (len);
}

void
V1L_Chunked(const struct worker *wrk)
{
	struct v1l *v1l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	v1l = wrk->v1l;
	CHECK_OBJ_NOTNULL(v1l, V1L_MAGIC);

	assert(v1l->ciov == v1l->siov);
	assert(v1l->siov >= 3);
	/*
	 * If there is no space for chunked header, a chunk of data and
	 * a chunk tail, we might as well flush right away.
	 */
	if (v1l->niov + 3 >= v1l->siov) {
		(void)V1L_Flush(wrk);
		VSC_C_main->http1_iovs_flush++;
	}
	v1l->siov--;
	v1l->ciov = v1l->niov++;
	v1l->cliov = 0;
	assert(v1l->ciov < v1l->siov);
	assert(v1l->niov < v1l->siov);
}

/*
 * XXX: It is not worth the complexity to attempt to get the
 * XXX: end of chunk into the V1L_Flush(), because most of the time
 * XXX: if not always, that is a no-op anyway, because the calling
 * XXX: code already called V1L_Flush() to release local storage.
 */

void
V1L_EndChunk(const struct worker *wrk)
{
	struct v1l *v1l;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	v1l = wrk->v1l;
	CHECK_OBJ_NOTNULL(v1l, V1L_MAGIC);

	assert(v1l->ciov < v1l->siov);
	(void)V1L_Flush(wrk);
	v1l->siov++;
	v1l->ciov = v1l->siov;
	v1l->niov = 0;
	v1l->cliov = 0;
	(void)V1L_Write(wrk, "0\r\n\r\n", -1);
}

/*--------------------------------------------------------------------
 * VDP using V1L
 */

static int v_matchproto_(vdp_bytes_f)
v1l_bytes(struct vdp_ctx *vdc, enum vdp_action act, void **priv,
    const void *ptr, ssize_t len)
{
	ssize_t wl = 0;

	CHECK_OBJ_NOTNULL(vdc, VDP_CTX_MAGIC);
	(void)priv;

	AZ(vdc->nxt);		/* always at the bottom of the pile */

	if (len > 0)
		wl = V1L_Write(vdc->wrk, ptr, len);
	if (act > VDP_NULL && V1L_Flush(vdc->wrk) != SC_NULL)
		return (-1);
	if (len != wl)
		return (-1);
	return (0);
}

const struct vdp * const VDP_v1l = &(struct vdp){
	.name =		"V1B",
	.bytes =	v1l_bytes,
};
