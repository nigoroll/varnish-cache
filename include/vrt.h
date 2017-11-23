/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2015 Varnish Software AS
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
 * Runtime support for compiled VCL programs and VMODs.
 *
 * NB: When this file is changed, lib/libvcc/generate.py *MUST* be rerun.
 */

#ifdef CACHE_H_INCLUDED
#  error "vrt.h included after cache.h - they are inclusive"
#endif

#ifdef VRT_H_INCLUDED
#  error "vrt.h included multiple times"
#endif
#define VRT_H_INCLUDED

#ifndef VDEF_H_INCLUDED
#  error "include vdef.h before vrt.h"
#endif

/***********************************************************************
 * Major and minor VRT API versions.
 *
 * Whenever something is added, increment MINOR version
 * Whenever something is deleted or changed in a way which is not
 * binary/load-time compatible, increment MAJOR version
 *
 *
 * 6.2 (scheduled for: 2018-03-15)
 *	VRT_Healthy() added
 *	VRT_VSC_Alloc() added
 *	VRT_VSC_Destroy() added
 *	struct director.event added
 *	struct director.destroy added
 * 6.1 (2017-09-15 aka 5.2)
 *	http_CollectHdrSep added
 *	VRT_purge modified (may fail a transaction, signature changed)
 * 6.0 (2017-03-15):
 *	VRT_hit_for_pass added
 *	VRT_ipcmp added
 *	VRT_Vmod_Init signature changed
 *	VRT_vcl_lookup removed
 *	VRT_vcl_get added
 *	VRT_vcl_rel added
 *	VRT_fail added
 *	WS_Reset and WS_Snapshot signatures changed
 *	WS_Front added
 *	WS_ReserveLumps added
 *	WS_Inside added
 *	WS_Assert_Allocated added
 * 5.0:
 *	Varnish 5.0 release "better safe than sorry" bump
 * 4.0:
 *	VCL_BYTES changed to long long
 *	VRT_CacheReqBody changed signature
 * 3.2:
 *	vrt_backend grew .proxy_header field
 *	vrt_ctx grew .sp field.
 *	vrt_acl type added
 */

#define VRT_MAJOR_VERSION	6U

#define VRT_MINOR_VERSION	2U

/***********************************************************************/

#include <stddef.h>		// NULL, size_t
#include <stdint.h>		// [u]int%d_t

struct VCL_conf;
struct busyobj;
struct director;
struct http;
struct req;
struct stevedore;
struct suckaddr;
struct vcl;
struct vmod;
struct vmod_priv;
struct vrt_acl;
struct vsb;
struct vsl_log;
struct ws;

/***********************************************************************
 * This is the central definition of the mapping from VCL types to
 * C-types.  The python scripts read these from here.
 * (alphabetic order)
 */

typedef const struct vrt_acl *			VCL_ACL;
typedef const struct director *			VCL_BACKEND;
typedef const struct vmod_priv *		VCL_BLOB;
typedef const char *				VCL_BODY;
typedef unsigned				VCL_BOOL;
typedef long long				VCL_BYTES;
typedef double					VCL_DURATION;
typedef const char *				VCL_ENUM;
typedef const struct gethdr_s *			VCL_HEADER;
typedef struct http *				VCL_HTTP;
typedef void					VCL_INSTANCE;
typedef long					VCL_INT;
typedef const struct suckaddr *			VCL_IP;
typedef const struct vrt_backend_probe *	VCL_PROBE;
typedef double					VCL_REAL;
typedef const struct stevedore *		VCL_STEVEDORE;
typedef const char *				VCL_STRING;
typedef double					VCL_TIME;
typedef struct vcl *				VCL_VCL;
typedef void					VCL_VOID;

/***********************************************************************
 * This is the composite argument we pass to compiled VCL and VRT
 * functions.
 */

struct vrt_ctx {
	unsigned			magic;
#define VRT_CTX_MAGIC			0x6bb8f0db

	unsigned			method;
	unsigned			*handling;

	struct vsb			*msg;	// Only in ...init()
	struct vsl_log			*vsl;
	struct vcl			*vcl;
	struct ws			*ws;

	struct sess			*sp;

	struct req			*req;
	struct http			*http_req;
	struct http			*http_req_top;
	struct http			*http_resp;
	struct http			*http_resp_top;

	struct busyobj			*bo;
	struct http			*http_bereq;
	struct http			*http_beresp;

	double				now;

	/*
	 * method specific argument:
	 *    hash:		struct VSHA256Context
	 *    synth+error:	struct vsb *
	 */
	void				*specific;
};

#define VRT_CTX		const struct vrt_ctx *ctx

/***********************************************************************
 * This is the interface structure to a compiled VMOD
 */

struct vmod_data {
	/* The version/id fields must be first, they protect the rest */
	unsigned			vrt_major;
	unsigned			vrt_minor;
	const char			*file_id;

	const char			*name;
	const void			*func;
	int				func_len;
	const char			*proto;
	const char			* const *spec;
	const char			*abi;
};

/***********************************************************************
 * Enum for events sent to compiled VCL and from there to Vmods
 */

enum vcl_event_e {
	VCL_EVENT_LOAD,
	VCL_EVENT_WARM,
	VCL_EVENT_COLD,
	VCL_EVENT_DISCARD,
};

/***********************************************************************/

extern const void * const vrt_magic_string_end;
extern const void * const vrt_magic_string_unset;

/***********************************************************************
 * We want the VCC to spit this structs out as const, but when VMODs
 * come up with them we want to clone them into malloc'ed space which
 * we can free again.
 * We collect all the knowledge here by macroizing the fields and make
 * a macro for handling them all.
 * See also:  cache_backend.h & cache_backend_cfg.c
 * One of those things...
 */

#define VRT_BACKEND_FIELDS(rigid)				\
	rigid char			*vcl_name;		\
	rigid char			*ipv4_addr;		\
	rigid char			*ipv6_addr;		\
	rigid char			*port;			\
	rigid char			*hosthdr;		\
	double				connect_timeout;	\
	double				first_byte_timeout;	\
	double				between_bytes_timeout;	\
	unsigned			max_connections;	\
	unsigned			proxy_header;

#define VRT_BACKEND_HANDLE()			\
	do {					\
		DA(vcl_name);			\
		DA(ipv4_addr);			\
		DA(ipv6_addr);			\
		DA(port);			\
		DA(hosthdr);			\
		DN(connect_timeout);		\
		DN(first_byte_timeout);		\
		DN(between_bytes_timeout);	\
		DN(max_connections);		\
		DN(proxy_header);		\
	} while(0)

struct vrt_backend {
	unsigned			magic;
#define VRT_BACKEND_MAGIC		0x4799ce6b
	VRT_BACKEND_FIELDS(const)
	const struct suckaddr		*ipv4_suckaddr;
	const struct suckaddr		*ipv6_suckaddr;
	const struct vrt_backend_probe	*probe;
};

#define VRT_BACKEND_PROBE_FIELDS(rigid)				\
	double				timeout;		\
	double				interval;		\
	unsigned			exp_status;		\
	unsigned			window;			\
	unsigned			threshold;		\
	unsigned			initial;

#define VRT_BACKEND_PROBE_HANDLE()		\
	do {					\
		DN(timeout);			\
		DN(interval);			\
		DN(exp_status);			\
		DN(window);			\
		DN(threshold);			\
		DN(initial);			\
	} while (0)

struct vrt_backend_probe {
	unsigned			magic;
#define VRT_BACKEND_PROBE_MAGIC		0x84998490
	const char			*url;
	const char			*request;
	VRT_BACKEND_PROBE_FIELDS(const)
};

/***********************************************************************
 * VRT_count() refers to this structure for coordinates into the VCL source.
 */

struct vrt_ref {
	unsigned	source;
	unsigned	offset;
	unsigned	line;
	unsigned	pos;
	const char	*token;
};

void VRT_count(VRT_CTX, unsigned);

/***********************************************************************
 * Implementation details of ACLs
 */

typedef int acl_match_f(VRT_CTX, const VCL_IP);

struct vrt_acl {
	unsigned	magic;
#define VRT_ACL_MAGIC	0x78329d96
	acl_match_f	*match;
};

void VRT_acl_log(VRT_CTX, const char *);
int VRT_acl_match(VRT_CTX, VCL_ACL, VCL_IP);

/***********************************************************************
 * Compile time regexp
 */

void VRT_re_init(void **, const char *);
void VRT_re_fini(void *);
int VRT_re_match(VRT_CTX, const char *, void *);

/***********************************************************************
 * Getting hold of the various struct http
 */

enum gethdr_e {
	HDR_REQ,
	HDR_REQ_TOP,
	HDR_RESP,
	HDR_RESP_TOP,
	HDR_OBJ,
	HDR_BEREQ,
	HDR_BERESP
};

struct gethdr_s {
	enum gethdr_e	where;
	const char	*what;
};

const char *VRT_hdr_where(const enum gethdr_e);
void VRT_hdr_fail(VRT_CTX, const struct gethdr_s * const,
    const char *err, const char *why);
struct http *VRT_http_ref_rw(VRT_CTX, enum gethdr_e, const char **why);
void VRT_http_deref_rw(struct http **);
const struct http *VRT_http_ref_ro(VRT_CTX, enum gethdr_e, const char **why);
void VRT_http_deref_ro(const struct http **);
const char *VRT_GetHdr(VRT_CTX, const struct gethdr_s *);

/***********************************************************************
 * req related
 */

VCL_BYTES VRT_CacheReqBody(VRT_CTX, VCL_BYTES maxsize);

/* Regexp related */

const char *VRT_regsub(VRT_CTX, int all, const char *, void *, const char *);
void VRT_ban_string(VRT_CTX, const char *);
unsigned VRT_purge(VRT_CTX, double ttl, double grace, double keep);

void VRT_synth(VRT_CTX, unsigned, const char *);
void VRT_hit_for_pass(VRT_CTX, VCL_DURATION);

void VRT_SetHdr(VRT_CTX, const struct gethdr_s *, const char *, ...);
void VRT_handling(VRT_CTX, unsigned hand);
void VRT_fail(VRT_CTX, const char *fmt, ...) __v_printflike(2,3);

void VRT_hashdata(VRT_CTX, const char *str, ...);

/* Simple stuff */
int VRT_strcmp(const char *s1, const char *s2);
void VRT_memmove(void *dst, const void *src, unsigned len);
int VRT_ipcmp(const struct suckaddr *sua1, const struct suckaddr *sua2);

void VRT_Rollback(VRT_CTX, const struct http *);

/* Synthetic pages */
void VRT_synth_page(VRT_CTX, const char *, ...);

/* Backend related */
struct director *VRT_new_backend(VRT_CTX, const struct vrt_backend *);
void VRT_delete_backend(VRT_CTX, struct director **);
int VRT_backend_healthy(VRT_CTX, struct director *);


/* cache_director.c */
int VRT_Healthy(VRT_CTX, VCL_BACKEND);

/* Suckaddr related */
int VRT_VSA_GetPtr(const struct suckaddr *sua, const unsigned char ** dst);

/* VMOD/Modules related */
int VRT_Vmod_Init(VRT_CTX, struct vmod **hdl, void *ptr, int len,
    const char *nm, const char *path, const char *file_id, const char *backup);
void VRT_Vmod_Fini(struct vmod **hdl);

/* VCL program related */
VCL_VCL VRT_vcl_get(VRT_CTX, const char *);
void VRT_vcl_rel(VRT_CTX, VCL_VCL);
void VRT_vcl_select(VRT_CTX, VCL_VCL);

typedef int vmod_event_f(VRT_CTX, struct vmod_priv *, enum vcl_event_e);

typedef void vmod_priv_free_f(void *);
struct vmod_priv {
	void			*priv;
	int			len;
	vmod_priv_free_f	*free;
};

struct vclref;
struct vclref * VRT_ref_vcl(VRT_CTX, const char *);
void VRT_rel_vcl(VRT_CTX, struct vclref **);

void VRT_priv_fini(const struct vmod_priv *p);
struct vmod_priv *VRT_priv_task(VRT_CTX, const void *vmod_id);
struct vmod_priv *VRT_priv_top(VRT_CTX, const void *vmod_id);

/* Stevedore related functions */
int VRT_Stv(const char *nm);
VCL_STEVEDORE VRT_stevedore(const char *nm);

/* Convert things to string */

char *VRT_IP_string(VRT_CTX, VCL_IP);
char *VRT_INT_string(VRT_CTX, VCL_INT);
char *VRT_REAL_string(VRT_CTX, VCL_REAL);
char *VRT_TIME_string(VRT_CTX, VCL_TIME);
const char *VRT_BOOL_string(VCL_BOOL);
const char *VRT_BACKEND_string(VCL_BACKEND);
const char *VRT_STEVEDORE_string(VCL_STEVEDORE);
const char *VRT_CollectString(VRT_CTX, const char *p, ...);

#ifdef va_start	// XXX: hackish
void *VRT_VSC_Alloc(const char *, size_t, size_t, const unsigned char *, size_t,
    const char *, va_list);
void VRT_VSC_Destroy(const char *, const void *);
#endif
