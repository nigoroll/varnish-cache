/*-
 * Copyright (c) 2015 Varnish Software AS
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
 * TCP connection pools.
 *
 */

#include "config.h"

#include <errno.h>
#include <stdlib.h>

#include "cache_varnishd.h"

#include "vsa.h"
#include "vtcp.h"
#include "vus.h"
#include "vtim.h"
#include "waiter/waiter.h"

#include "cache_tcp_pool.h"
#include "cache_pool.h"
#include "VSC_vcp.h"

struct conn_pool;

/*--------------------------------------------------------------------
 */

struct pfd {
	unsigned		magic;
#define PFD_MAGIC		0x0c5e6593
	int			fd;
	VTAILQ_ENTRY(pfd)	list;
	const void		*priv;
	uint8_t			state;
	struct waited		waited[1];
	struct conn_pool	*conn_pool;

	pthread_cond_t		*cond;
};

/*--------------------------------------------------------------------
 */

typedef int cp_open_f(const struct conn_pool *, double tmo, const void **privp);
typedef void cp_close_f(struct pfd *);
typedef int cp_cmp_f(const struct conn_pool *, const void *priv);
typedef void cp_name_f(const struct pfd *, char *, unsigned, char *, unsigned);

struct cp_methods {
	cp_open_f				*open;
	cp_close_f				*close;
	cp_cmp_f				*cmp;
	cp_name_f				*local_name;
	cp_name_f				*remote_name;
};

struct conn_pool {
	unsigned				magic;
#define CONN_POOL_MAGIC				0x85099bc3

	const struct cp_methods			*methods;

	const void				*id;
	void					*priv;

	VTAILQ_ENTRY(conn_pool)			list;
	int					refcnt;
	struct lock				mtx;
	struct VSC_vcp				*stats;
	struct vsc_seg				*vsc;

	/* length: stat->n_conn */
	VTAILQ_HEAD(, pfd)			connlist;

	/* length: stat->n_kill */
	VTAILQ_HEAD(, pfd)			killlist;
};

struct tcp_pool {
	unsigned				magic;
#define TCP_POOL_MAGIC				0x28b0e42a

	struct suckaddr				*ip4;
	struct suckaddr				*ip6;
	char					*uds;
	struct conn_pool			cp[1];
};

static struct lock		conn_pools_mtx;
static VTAILQ_HEAD(, conn_pool)	conn_pools =
    VTAILQ_HEAD_INITIALIZER(conn_pools);

/*--------------------------------------------------------------------
 */

unsigned
PFD_State(const struct pfd *p)
{
	CHECK_OBJ_NOTNULL(p, PFD_MAGIC);
	return (p->state);
}

int *
PFD_Fd(struct pfd *p)
{
	CHECK_OBJ_NOTNULL(p, PFD_MAGIC);
	return (&(p->fd));
}

void
PFD_LocalName(const struct pfd *p, char *abuf, unsigned alen, char *pbuf,
	      unsigned plen)
{
	CHECK_OBJ_NOTNULL(p, PFD_MAGIC);
	CHECK_OBJ_NOTNULL(p->conn_pool, CONN_POOL_MAGIC);
	p->conn_pool->methods->local_name(p, abuf, alen, pbuf, plen);
}

void
PFD_RemoteName(const struct pfd *p, char *abuf, unsigned alen, char *pbuf,
	       unsigned plen)
{
	CHECK_OBJ_NOTNULL(p, PFD_MAGIC);
	CHECK_OBJ_NOTNULL(p->conn_pool, CONN_POOL_MAGIC);
	p->conn_pool->methods->remote_name(p, abuf, alen, pbuf, plen);
}

/*--------------------------------------------------------------------
 * Waiter-handler
 */

static void  v_matchproto_(waiter_handle_f)
vcp_handle(struct waited *w, enum wait_event ev, double now)
{
	struct pfd *pfd;
	struct conn_pool *cp;

	CAST_OBJ_NOTNULL(pfd, w->priv1, PFD_MAGIC);
	(void)ev;
	(void)now;
	CHECK_OBJ_NOTNULL(pfd->conn_pool, CONN_POOL_MAGIC);
	cp = pfd->conn_pool;

	Lck_Lock(&cp->mtx);

	switch (pfd->state) {
	case PFD_STATE_STOLEN:
		pfd->state = PFD_STATE_USED;
		VTAILQ_REMOVE(&cp->connlist, pfd, list);
		AN(pfd->cond);
		AZ(pthread_cond_signal(pfd->cond));
		break;
	case PFD_STATE_AVAIL:
		cp->methods->close(pfd);
		VTAILQ_REMOVE(&cp->connlist, pfd, list);
		cp->stats->closed++;
		cp->stats->n_conn--;
		FREE_OBJ(pfd);
		break;
	case PFD_STATE_CLEANUP:
		cp->methods->close(pfd);
		cp->stats->closed++;
		cp->stats->n_kill--;
		VTAILQ_REMOVE(&cp->killlist, pfd, list);
		memset(pfd, 0x11, sizeof *pfd);
		free(pfd);
		break;
	default:
		WRONG("Wrong pfd state");
	}
	Lck_Unlock(&cp->mtx);
}

/*--------------------------------------------------------------------
 */

static struct conn_pool *
VCP_Ref(const void *id, const void *priv)
{
	struct conn_pool *cp;

	Lck_Lock(&conn_pools_mtx);
	VTAILQ_FOREACH(cp, &conn_pools, list) {
		assert(cp->refcnt > 0);
		if (cp->id != id)
			continue;
		if (cp->methods->cmp(cp, priv))
			continue;
		cp->refcnt++;
		Lck_Unlock(&conn_pools_mtx);
		return (cp);
	}
	Lck_Unlock(&conn_pools_mtx);
	return (NULL);
}

/*--------------------------------------------------------------------
 */

static void *
VCP_New(struct conn_pool *cp, const void *id, void *priv,
    const struct cp_methods *cm)
{
	struct tcp_pool *tp = priv;
	char abuf[VTCP_ADDRBUFSIZE];
	char pbuf[VTCP_PORTBUFSIZE];
	const struct suckaddr *addr;

	AN(cp);
	AN(cm);
	AN(cm->open);
	AN(cm->close);
	AN(cm->cmp);

	INIT_OBJ(cp, CONN_POOL_MAGIC);
	cp->id = id;
	cp->priv = priv;
	cp->methods = cm;
	cp->refcnt = 1;
	Lck_New(&cp->mtx, lck_tcp_pool);
	VTAILQ_INIT(&cp->connlist);
	VTAILQ_INIT(&cp->killlist);

	// XXX layering
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	// XXX common cluster/seg
	if (tp->uds != NULL)
		cp->stats = VSC_vcp_New(NULL, &cp->vsc, "%p.%s", id, tp->uds);
	else {
		if (! tp->ip4 || (cache_param->prefer_ipv6 && tp->ip6))
			addr = tp->ip6;
		else
			addr = tp->ip4;

		AN(addr);
		VTCP_name(addr, abuf, sizeof abuf, pbuf, sizeof pbuf);
		cp->stats = VSC_vcp_New(NULL, &cp->vsc, "%p.%s:%s", id,
					abuf, pbuf);
	}

	Lck_Lock(&conn_pools_mtx);
	VTAILQ_INSERT_HEAD(&conn_pools, cp, list);
	VSC_C_main->vcp_create++;
	Lck_Unlock(&conn_pools_mtx);

	return (priv);
}


/*--------------------------------------------------------------------
 */

static void
VCP_AddRef(struct conn_pool *cp)
{
	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);

	Lck_Lock(&conn_pools_mtx);
	assert(cp->refcnt > 0);
	cp->refcnt++;
	Lck_Unlock(&conn_pools_mtx);
}

/*--------------------------------------------------------------------
 * Release Conn pool, destroy if last reference.
 */

static int
VCP_Rel(struct conn_pool *cp)
{
	struct pfd *pfd, *pfd2;

	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);

	Lck_Lock(&conn_pools_mtx);
	assert(cp->refcnt > 0);
	if (--cp->refcnt > 0) {
		Lck_Unlock(&conn_pools_mtx);
		return (1);
	}
	AZ(cp->stats->n_used);
	VTAILQ_REMOVE(&conn_pools, cp, list);
	VSC_C_main->vcp_destroy++;
	Lck_Unlock(&conn_pools_mtx);

	Lck_Lock(&cp->mtx);
	VTAILQ_FOREACH_SAFE(pfd, &cp->connlist, list, pfd2) {
		VTAILQ_REMOVE(&cp->connlist, pfd, list);
		cp->stats->n_conn--;
		assert(pfd->state == PFD_STATE_AVAIL);
		pfd->state = PFD_STATE_CLEANUP;
		(void)shutdown(pfd->fd, SHUT_WR);
		VTAILQ_INSERT_TAIL(&cp->killlist, pfd, list);
		cp->stats->n_kill++;
	}
	while (cp->stats->n_kill) {
		Lck_Unlock(&cp->mtx);
		(void)usleep(20000);
		Lck_Lock(&cp->mtx);
	}
	Lck_Unlock(&cp->mtx);
	Lck_Delete(&cp->mtx);
	AZ(cp->stats->n_conn);
	AZ(cp->stats->n_kill);
	cp->stats = NULL;
	VSC_vcp_Destroy(&cp->vsc);
	AZ(cp->vsc);
	return (0);
}

/*--------------------------------------------------------------------
 * Open a new connection from pool.  This is a distinct function since
 * probing cannot use a recycled connection.
 */

static int
VCP_Open(struct conn_pool *cp, double tmo, const void **privp)
{
	int r;
	double h;

	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);

	r = cp->methods->open(cp, tmo, privp);

	if (r >= 0) {
		cp->stats->opened++;
		return (r);
	}

	/* stats access unprotected */
	switch (errno) {
	case EACCES:
	case EPERM:
		cp->stats->fail_eacces++;
		break;
	case EADDRNOTAVAIL:
		cp->stats->fail_eaddrnotavail++;
		break;
	case ECONNREFUSED:
		cp->stats->fail_econnrefused++;
		break;
	case ENETUNREACH:
		cp->stats->fail_enetunreach++;
		break;
	case ETIMEDOUT:
		cp->stats->fail_etimedout++;
		break;
	default:
		cp->stats->fail_other++;
		// XXX name
		VSL(SLT_Debug, 0, "VTP open error %d (%s)",
			errno, strerror(errno));
	}
	cp->stats->fail++;

	return (r);
}

/*--------------------------------------------------------------------
 * Recycle a connection.
 */

static void
VCP_Recycle(const struct worker *wrk, struct pfd **pfdp)
{
	struct pfd *pfd;
	struct conn_pool *cp;
	int i = 0;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	pfd = *pfdp;
	*pfdp = NULL;
	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	cp = pfd->conn_pool;
	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);

	assert(pfd->state == PFD_STATE_USED);
	assert(pfd->fd > 0);

	Lck_Lock(&cp->mtx);
	cp->stats->n_used--;

	pfd->waited->priv1 = pfd;
	pfd->waited->fd = pfd->fd;
	pfd->waited->idle = VTIM_real();
	pfd->state = PFD_STATE_AVAIL;
	pfd->waited->func = vcp_handle;
	pfd->waited->tmo = &cache_param->backend_idle_timeout;
	if (Wait_Enter(wrk->pool->waiter, pfd->waited)) {
		cp->methods->close(pfd);
		memset(pfd, 0x33, sizeof *pfd);
		free(pfd);
		cp->stats->recycle_fail++;
		cp->stats->closed++;
		pfd = NULL;
	} else {
		VTAILQ_INSERT_HEAD(&cp->connlist, pfd, list);
		cp->stats->recycle++;
		i++;
	}

	if (pfd != NULL)
		cp->stats->n_conn++;
	Lck_Unlock(&cp->mtx);

	if (i && DO_DEBUG(DBG_VTC_MODE)) {
		/*
		 * In varnishtest we do not have the luxury of using
		 * multiple backend connections, so whenever we end up
		 * in the "pending" case, take a short nap to let the
		 * waiter catch up and put the pfd back into circulations.
		 *
		 * In particular ESI:include related tests suffer random
		 * failures without this.
		 *
		 * In normal operation, the only effect is that we will
		 * have N+1 backend connections rather than N, which is
		 * entirely harmless.
		 */
		(void)usleep(10000);
	}
}


/*--------------------------------------------------------------------
 * Close a connection.
 */

static void
VCP_Close(struct pfd **pfdp)
{
	struct pfd *pfd;
	struct conn_pool *cp;

	pfd = *pfdp;
	*pfdp = NULL;
	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	cp = pfd->conn_pool;
	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);

	assert(pfd->fd > 0);

	Lck_Lock(&cp->mtx);
	assert(pfd->state == PFD_STATE_USED || pfd->state == PFD_STATE_STOLEN);
	cp->stats->n_used--;
	if (pfd->state == PFD_STATE_STOLEN) {
		(void)shutdown(pfd->fd, SHUT_RDWR);
		VTAILQ_REMOVE(&cp->connlist, pfd, list);
		pfd->state = PFD_STATE_CLEANUP;
		VTAILQ_INSERT_HEAD(&cp->killlist, pfd, list);
		cp->stats->n_kill++;
	} else {
		assert(pfd->state == PFD_STATE_USED);
		cp->methods->close(pfd);
		cp->stats->closed++;
		memset(pfd, 0x44, sizeof *pfd);
		free(pfd);
	}
	Lck_Unlock(&cp->mtx);
}

/*--------------------------------------------------------------------
 * Get a connection
 */

static struct pfd *
VCP_Get(struct conn_pool *cp, double tmo, struct worker *wrk,
    unsigned force_fresh)
{
	struct pfd *pfd;

	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);
	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);

	Lck_Lock(&cp->mtx);
	pfd = VTAILQ_FIRST(&cp->connlist);
	CHECK_OBJ_ORNULL(pfd, PFD_MAGIC);
	if (force_fresh || pfd == NULL || pfd->state == PFD_STATE_STOLEN)
		pfd = NULL;
	else {
		assert(pfd->conn_pool == cp);
		assert(pfd->state == PFD_STATE_AVAIL);
		VTAILQ_REMOVE(&cp->connlist, pfd, list);
		VTAILQ_INSERT_TAIL(&cp->connlist, pfd, list);
		cp->stats->n_conn--;
		VSC_C_main->backend_reuse++;
		pfd->state = PFD_STATE_STOLEN;
		pfd->cond = &wrk->cond;
	}
	cp->stats->n_used++;			// Opening mostly works
	Lck_Unlock(&cp->mtx);

	if (pfd != NULL)
		return (pfd);

	ALLOC_OBJ(pfd, PFD_MAGIC);
	AN(pfd);
	INIT_OBJ(pfd->waited, WAITED_MAGIC);
	pfd->state = PFD_STATE_USED;
	pfd->conn_pool = cp;
	pfd->fd = VCP_Open(cp, tmo, &pfd->priv);
	if (pfd->fd < 0) {
		FREE_OBJ(pfd);
		Lck_Lock(&cp->mtx);
		cp->stats->n_used--;		// Nope, didn't work after all.
		Lck_Unlock(&cp->mtx);
	} else
		VSC_C_main->backend_conn++;

	return (pfd);
}

/*--------------------------------------------------------------------
 */

static int
VCP_Wait(struct worker *wrk, struct pfd *pfd, double tmo)
{
	struct conn_pool *cp;
	int r;

	CHECK_OBJ_NOTNULL(wrk, WORKER_MAGIC);
	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	cp = pfd->conn_pool;
	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);
	assert(pfd->cond == &wrk->cond);
	Lck_Lock(&cp->mtx);
	while (pfd->state == PFD_STATE_STOLEN) {
		r = Lck_CondWait(&wrk->cond, &cp->mtx, tmo);
		if (r != 0) {
			if (r == EINTR)
				continue;
			assert(r == ETIMEDOUT);
			Lck_Unlock(&cp->mtx);
			return (1);
		}
	}
	assert(pfd->state == PFD_STATE_USED);
	pfd->cond = NULL;
	Lck_Unlock(&cp->mtx);

	return (0);
}

/*--------------------------------------------------------------------
 */

struct vtp_cs {
	unsigned			magic;
#define VTP_CS_MAGIC			0xc1e40447
	const struct suckaddr		*ip4;
	const struct suckaddr		*ip6;
	const char			*uds;
};

static inline int
tmo2msec(double tmo)
{
	return ( (int)floor(tmo * 1000.0) );
}

static int v_matchproto_(cp_open_f)
vtp_open(const struct conn_pool *cp, double tmo, const void **privp)
{
	int s;
	int msec;
	struct tcp_pool *tp;

	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);
	CAST_OBJ_NOTNULL(tp, cp->priv, TCP_POOL_MAGIC);

	msec = tmo2msec(tmo);
	if (cache_param->prefer_ipv6) {
		*privp = tp->ip6;
		s = VTCP_connect(tp->ip6, msec);
		if (s >= 0)
			return (s);
	}
	*privp = tp->ip4;
	s = VTCP_connect(tp->ip4, msec);
	if (s >= 0)
		return (s);
	if (!cache_param->prefer_ipv6) {
		*privp = tp->ip6;
		s = VTCP_connect(tp->ip6, msec);
	}
	return (s);
}

static void v_matchproto_(cp_close_f)
vtp_close(struct pfd *pfd)
{

	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	VTCP_close(&pfd->fd);
}

static int v_matchproto_(cp_cmp_f)
vtp_cmp(const struct conn_pool *cp, const void *priv)
{
	const struct vtp_cs *vcs;
	const struct tcp_pool *tp;

	CAST_OBJ_NOTNULL(vcs, priv, VTP_CS_MAGIC);
	CAST_OBJ_NOTNULL(tp, cp->priv, TCP_POOL_MAGIC);
	if (tp->ip4 == NULL && vcs->ip4 != NULL)
		return (1);
	if (tp->ip4 != NULL && vcs->ip4 == NULL)
		return (1);
	if (tp->ip6 == NULL && vcs->ip6 != NULL)
		return (1);
	if (tp->ip6 != NULL && vcs->ip6 == NULL)
		return (1);
	if (tp->ip4 != NULL && vcs->ip4 != NULL &&
	    VSA_Compare(tp->ip4, vcs->ip4))
		return (1);
	if (tp->ip6 != NULL && vcs->ip6 != NULL &&
	    VSA_Compare(tp->ip6, vcs->ip6))
		return (1);
	return (0);
}

static void v_matchproto_(cp_name_f)
vtp_local_name(const struct pfd *pfd, char *addr, unsigned alen, char *pbuf,
	       unsigned plen)
{
	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	VTCP_myname(pfd->fd, addr, alen, pbuf, plen);
}

static void v_matchproto_(cp_name_f)
vtp_remote_name(const struct pfd *pfd, char *addr, unsigned alen, char *pbuf,
		unsigned plen)
{
	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	VTCP_hisname(pfd->fd, addr, alen, pbuf, plen);
}

static const struct cp_methods vtp_methods = {
	.open = vtp_open,
	.close = vtp_close,
	.cmp = vtp_cmp,
	.local_name = vtp_local_name,
	.remote_name = vtp_remote_name,
};

/*--------------------------------------------------------------------
 */

static int v_matchproto_(cp_open_f)
vus_open(const struct conn_pool *cp, double tmo, const void **privp)
{
	int s;
	int msec;
	struct tcp_pool *tp;

	CHECK_OBJ_NOTNULL(cp, CONN_POOL_MAGIC);
	CAST_OBJ_NOTNULL(tp, cp->priv, TCP_POOL_MAGIC);
	AN(tp->uds);

	msec = tmo2msec(tmo);
	*privp = bogo_ip;
	s = VUS_connect(tp->uds, msec);
	return (s);
}

static int v_matchproto_(cp_cmp_f)
vus_cmp(const struct conn_pool *cp, const void *priv)
{
	const struct vtp_cs *vcs;
	const struct tcp_pool *tp;

	CAST_OBJ_NOTNULL(vcs, priv, VTP_CS_MAGIC);
	CAST_OBJ_NOTNULL(tp, cp->priv, TCP_POOL_MAGIC);
	if (tp->uds != NULL && vcs->uds != NULL)
		return (strcmp(tp->uds, vcs->uds));
	return (1);
}

static void v_matchproto_(cp_name_f)
vus_name(const struct pfd *pfd, char *addr, unsigned alen, char *pbuf,
	 unsigned plen)
{
	(void) pfd;
	assert(alen > strlen("0.0.0.0"));
	assert(plen > 1);
	strcpy(addr, "0.0.0.0");
	strcpy(pbuf, "0");
}

static const struct cp_methods vus_methods = {
	.open = vus_open,
	.close = vtp_close,
	.cmp = vus_cmp,
	.local_name = vus_name,
	.remote_name = vus_name,
};

/*--------------------------------------------------------------------
 * Reference a TCP pool given by {ip4, ip6} pair or a UDS.  Create if
 * it doesn't exist already.
 */

struct tcp_pool *
VTP_Ref(const struct suckaddr *ip4, const struct suckaddr *ip6, const char *uds,
	const void *id)
{
	struct tcp_pool *tp;
	struct conn_pool *cp;
	struct vtp_cs vcs;
	const struct cp_methods *methods;

	assert((uds != NULL && ip4 == NULL && ip6 == NULL) ||
	    (uds == NULL && (ip4 != NULL || ip6 != NULL)));
	INIT_OBJ(&vcs, VTP_CS_MAGIC);
	vcs.ip4 = ip4;
	vcs.ip6 = ip6;
	vcs.uds = uds;

	cp = VCP_Ref(id, &vcs);
	if (cp != NULL)
		return (cp->priv);

	ALLOC_OBJ(tp, TCP_POOL_MAGIC);
	AN(tp);
	if (uds != NULL) {
		methods = &vus_methods;
		tp->uds = strdup(uds);
	}
	else {
		methods = &vtp_methods;
		if (ip4 != NULL)
			tp->ip4 = VSA_Clone(ip4);
		if (ip6 != NULL)
			tp->ip6 = VSA_Clone(ip6);
	}
	return(VCP_New(tp->cp, id, tp, methods));
}

/*--------------------------------------------------------------------
 * Add a reference to a tcp_pool
 */

void
VTP_AddRef(struct tcp_pool *tp)
{
	CHECK_OBJ_NOTNULL(tp, TCP_POOL_MAGIC);
	VCP_AddRef(tp->cp);
}

/*--------------------------------------------------------------------
 * Release TCP pool, destroy if last reference.
 */

void
VTP_Rel(struct tcp_pool **tpp)
{
	struct tcp_pool *tp;

	TAKE_OBJ_NOTNULL(tp, tpp, TCP_POOL_MAGIC);
	if (VCP_Rel(tp->cp))
		return;

	free(tp->ip4);
	free(tp->ip6);
	free(tp->uds);
	FREE_OBJ(tp);
}

/*--------------------------------------------------------------------
 * Open a new connection from pool.
 */

int
VTP_Open(struct tcp_pool *tp, double tmo, const void **privp)
{
	return (VCP_Open(tp->cp, tmo, privp));
}

/*--------------------------------------------------------------------
 * Recycle a connection.
 */

void
VTP_Recycle(const struct worker *wrk, struct pfd **pfdp)
{

	VCP_Recycle(wrk, pfdp);
}

/*--------------------------------------------------------------------
 * Close a connection.
 */

void
VTP_Close(struct pfd **pfdp)
{

	VCP_Close(pfdp);
}

/*--------------------------------------------------------------------
 * Get a connection
 */

struct pfd *
VTP_Get(struct tcp_pool *tp, double tmo, struct worker *wrk,
    unsigned force_fresh)
{

	return VCP_Get(tp->cp, tmo, wrk, force_fresh);
}

/*--------------------------------------------------------------------
 */

int
VTP_Wait(struct worker *wrk, struct pfd *pfd, double tmo)
{
	return (VCP_Wait(wrk, pfd, tmo));
}

/*--------------------------------------------------------------------
 */

const struct suckaddr *
VTP_getip(struct pfd *pfd)
{
	struct tcp_pool *tp;

	CHECK_OBJ_NOTNULL(pfd, PFD_MAGIC);
	CAST_OBJ_NOTNULL(tp, pfd->conn_pool->priv, TCP_POOL_MAGIC);
	return (pfd->priv);
}

/*--------------------------------------------------------------------*/

void
VTP_Init(void)
{
	Lck_New(&conn_pools_mtx, lck_tcp_pool);
}
