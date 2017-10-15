/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
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
 * XXX: charge bytes to srcaddr
 */

#include "config.h"

#include <poll.h>
#include <stdio.h>
#include <netinet/in.h>

#include "cache.h"

#include "cache_backend.h"
#include "vtcp.h"
#include "vtim.h"
#include "vend.h"
#include "vsa.h"

static struct lock pipestat_mtx;

struct acct_pipe {
	uint64_t	req;
	uint64_t	bereq;
	uint64_t	in;
	uint64_t	out;
};

static int
rdf(int fd0, int fd1, uint64_t *pcnt)
{
	int i, j;
	char buf[BUFSIZ], *p;

	i = read(fd0, buf, sizeof buf);
	if (i <= 0)
		return (1);
	for (p = buf; i > 0; i -= j, p += j) {
		j = write(fd1, p, i);
		if (j <= 0)
			return (1);
		*pcnt += j;
		if (i != j)
			(void)usleep(100000);		/* XXX hack */
	}
	return (0);
}

static void
pipecharge(struct req *req, const struct acct_pipe *a, struct VSC_C_vbe *b)
{

	VSLb(req->vsl, SLT_PipeAcct, "%ju %ju %ju %ju",
	    (uintmax_t)a->req,
	    (uintmax_t)a->bereq,
	    (uintmax_t)a->in,
	    (uintmax_t)a->out);

	Lck_Lock(&pipestat_mtx);
	VSC_C_main->s_pipe_hdrbytes += a->req;
	VSC_C_main->s_pipe_in += a->in;
	VSC_C_main->s_pipe_out += a->out;
	if (b != NULL) {
		b->pipe_hdrbytes += a->bereq;
		b->pipe_out += a->in;
		b->pipe_in += a->out;
	}
	Lck_Unlock(&pipestat_mtx);
}

union proxy_addr {
	struct {        /* for TCP/UDP over IPv4, len = 12 */
		uint32_t src_addr;
		uint32_t dst_addr;
		uint16_t src_port;
		uint16_t dst_port;
	} ip4;
	struct {        /* for TCP/UDP over IPv6, len = 36 */
		uint8_t  src_addr[16];
		uint8_t  dst_addr[16];
		uint16_t src_port;
		uint16_t dst_port;
	} ip6;
};


struct proxy_hdr_v2 {
	uint8_t sig[12];  /* hex 0D 0A 0D 0A 00 0D 0A 51 55 49 54 0A */
	uint8_t ver_cmd;  /* protocol version and command */
	uint8_t fam;      /* protocol family and address */
	uint16_t len;     /* number of following bytes part of the header */
	union proxy_addr addr;
};

/*
 * to use WRW_ / writev, we cannot have the extent to send on the
 * stack, so we construct our proxy_addr on workspace
 */
static size_t
proxyhdr(struct worker *w, struct ws *ws, const struct sockaddr *server,
    const struct sockaddr *client)
{
	size_t l;
	struct proxy_hdr_v2 *hdr;

	/* over-allocing for the ipv4 case */
	hdr = (void *)WS_Alloc(ws, sizeof(*hdr));
	if (hdr == NULL)
		return 0;

	hdr->sig[0]  = '\r';
	hdr->sig[1]  = '\n';
	hdr->sig[2]  = '\r';
	hdr->sig[3]  = '\n';
	hdr->sig[4]  = '\0';
	hdr->sig[5]  = '\r';
	hdr->sig[6]  = '\n';
	hdr->sig[7]  = 'Q';
	hdr->sig[8]  = 'U';
	hdr->sig[9]  = 'I';
	hdr->sig[10] = 'T';
	hdr->sig[11] = '\n';

	hdr->ver_cmd = 0x21; /* PROXY2 PROXY */

	/* we just hardcode SOCK_STREAM - 0x1 in fam */
	switch (client->sa_family) {
	case AF_INET:
		hdr->fam = 0x11;
		hdr->len = 12;
		hdr->addr.ip4.src_addr =
		    ((const struct sockaddr_in *)client)->sin_addr.s_addr;
		hdr->addr.ip4.src_port =
		    ((const struct sockaddr_in *)client)->sin_port;
		hdr->addr.ip4.dst_addr =
		    ((const struct sockaddr_in *)server)->sin_addr.s_addr;
		hdr->addr.ip4.dst_port =
		    ((const struct sockaddr_in *)server)->sin_port;
		break;
	case AF_INET6:
		hdr->fam = 0x21;
		hdr->len = 36;
		memcpy(hdr->addr.ip6.src_addr,
		    &((const struct sockaddr_in6 *)client)->sin6_addr, 16);
		hdr->addr.ip6.src_port =
		    ((const struct sockaddr_in6 *)client)->sin6_port;
		memcpy(hdr->addr.ip6.dst_addr,
		    &((const struct sockaddr_in6 *)server)->sin6_addr, 16);
		hdr->addr.ip6.dst_port =
		    ((const struct sockaddr_in6 *)server)->sin6_port;
		break;
	default:
		INCOMPL();
	}

	l = sizeof(*hdr) - sizeof(union proxy_addr) + hdr->len;

	vbe16enc(&hdr->len, hdr->len);

	return WRW_Write(w, hdr, l);
}

void
PipeRequest(struct req *req, struct busyobj *bo, const int proxy)
{
	struct vbc *vc;
	struct worker *wrk;
	struct pollfd fds[2];
	int i;
	struct acct_pipe acct_pipe;
	ssize_t hdrbytes = 0;

	CHECK_OBJ_NOTNULL(req, REQ_MAGIC);
	CHECK_OBJ_NOTNULL(req->sp, SESS_MAGIC);
	wrk = req->wrk;
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(bo, BUSYOBJ_MAGIC);

	req->res_mode = RES_PIPE;

	memset(&acct_pipe, 0, sizeof acct_pipe);
	acct_pipe.req = req->acct.req_hdrbytes;
	req->acct.req_hdrbytes = 0;

	if (bo->director == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "No backend");
		pipecharge(req, &acct_pipe, NULL);
		SES_Close(req->sp, SC_TX_ERROR);
		return;
	}

	vc = VDI_GetFd(bo);
	if (vc == NULL) {
		VSLb(bo->vsl, SLT_FetchError, "no backend connection");
		pipecharge(req, &acct_pipe, NULL);
		SES_Close(req->sp, SC_OVERLOAD);
		return;
	}
	bo->vbc = vc;		/* For panic dumping */
	(void)VTCP_blocking(vc->fd);

	WRW_Reserve(wrk, &vc->fd, bo->vsl, req->t_req);

	if (proxy) {
		socklen_t ls, lc;

		const struct sockaddr *server =
		    VSA_Get_Sockaddr(sess_local_addr(req->sp), &ls);
		const struct sockaddr *client =
		    VSA_Get_Sockaddr(sess_remote_addr(req->sp), &lc);

		assert(ls == lc);

		assert(server->sa_family != AF_UNSPEC);

		i = proxyhdr(wrk, req->sp->ws, server, client);

		if (i == 0) {
			VSLb(bo->vsl, SLT_FetchError, "pipe - proxy");
			pipecharge(req, &acct_pipe, NULL);
			SES_Close(req->sp, SC_TX_ERROR);
			return;
		}
		hdrbytes += i;
	}

	hdrbytes += HTTP1_Write(wrk, bo->bereq, HTTP1_Req);

	if (req->htc->pipeline.b != NULL)
		(void)WRW_Write(wrk, req->htc->pipeline.b,
		    Tlen(req->htc->pipeline));

	i = WRW_FlushRelease(wrk, &acct_pipe.bereq);
	if (acct_pipe.bereq > hdrbytes) {
		acct_pipe.in = acct_pipe.bereq - hdrbytes;
		acct_pipe.bereq = hdrbytes;
	}

	VSLb_ts_req(req, "Pipe", W_TIM_real(wrk));

	if (i) {
		pipecharge(req, &acct_pipe, vc->backend->vsc);
		SES_Close(req->sp, SC_TX_PIPE);
		VDI_CloseFd(&vc, NULL);
		return;
	}

	memset(fds, 0, sizeof fds);

	// XXX: not yet (void)VTCP_linger(vc->fd, 0);
	fds[0].fd = vc->fd;
	fds[0].events = POLLIN | POLLERR;

	// XXX: not yet (void)VTCP_linger(req->sp->fd, 0);
	fds[1].fd = req->sp->fd;
	fds[1].events = POLLIN | POLLERR;

	while (fds[0].fd > -1 || fds[1].fd > -1) {
		fds[0].revents = 0;
		fds[1].revents = 0;
		i = poll(fds, 2, (int)(cache_param->pipe_timeout * 1e3));
		if (i < 1)
			break;
		if (fds[0].revents &&
		    rdf(vc->fd, req->sp->fd, &acct_pipe.out)) {
			if (fds[1].fd == -1)
				break;
			(void)shutdown(vc->fd, SHUT_RD);
			(void)shutdown(req->sp->fd, SHUT_WR);
			fds[0].events = 0;
			fds[0].fd = -1;
		}
		if (fds[1].revents &&
		    rdf(req->sp->fd, vc->fd, &acct_pipe.in)) {
			if (fds[0].fd == -1)
				break;
			(void)shutdown(req->sp->fd, SHUT_RD);
			(void)shutdown(vc->fd, SHUT_WR);
			fds[1].events = 0;
			fds[1].fd = -1;
		}
	}
	VSLb_ts_req(req, "PipeSess", W_TIM_real(wrk));
	pipecharge(req, &acct_pipe, vc->backend->vsc);
	SES_Close(req->sp, SC_TX_PIPE);
	VDI_CloseFd(&vc, NULL);
	bo->vbc = NULL;
}

/*--------------------------------------------------------------------*/

void
Pipe_Init(void)
{

	Lck_New(&pipestat_mtx, lck_pipestat);
}
