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
 */

/* from libvarnish/vtim.c */
#define VTIM_FORMAT_SIZE 30

void VTIM_format(double t, char *p);
double VTIM_parse(const char *p);
double VTIM_mono(void);
double VTIM_real(void);
void VTIM_sleep(double t);
struct timespec VTIM_timespec(double t);
struct timeval VTIM_timeval(double t);

/*
 * VTM API
 * we're using struct typedefs to get strong typing
 * of the distinct time representations:
 * - real time
 * - monotonic time
 * - durations
 */
typedef struct {
	double mt;
} vtm_mono;
typedef struct {
	double rt;
} vtm_real;
typedef struct {
	double d;
} vtm_dur;

// real
vtm_real VTM_real(void);
vtm_real VTM_parse(const char *p);
void VTM_format(const vtm_real t, char *p);

static inline vtm_real
VTM_real_nan(void) {
	const vtm_real t = { 0 };
	return (t);
}

// mono
vtm_mono VTM_mono(void);
static inline vtm_mono
VTM_mono_nan(void) {
	const vtm_mono t = { 0 };
	return (t);
}


// dur
void VTM_sleep(const vtm_dur d);
static inline unsigned long
VTM_s(const vtm_dur d) {
	return (d.d);
}
static inline unsigned long
VTM_ms(const vtm_dur d) {
	return (d.d * 1e3);
}
static inline unsigned long
VTM_us(const vtm_dur d) {
	return (d.d * 1e6);
}
static inline unsigned long
VTM_ns(const vtm_dur d) {
	return (d.d * 1e9);
}

/*
 * inference for arithmatic
 *
 * tim := real | mono
 *
 * tim +- dur -> tim
 * tim - tim -> dur
 *
 * cmpop2 := == != < > <= >=
 *
 */

#define vtm_mono_memb mt
#define vtm_real_memb rt
#define vtm_dur_memb  d

#define vtm_dur_op(type, name, op)					\
	static inline vtm_ ## type					\
	VTM_ ## type ## _ ## name(const vtm_ ## type t, const vtm_dur d) \
	{								\
		vtm_ ## type r;						\
		r.vtm_ ## type ## _memb = t.vtm_ ## type ## _memb op d.d; \
		return (r);						\
	}

#define vtm_diff(type)							\
	static inline vtm_dur						\
	VTM_ ## type ## _diff(const vtm_ ## type a1, const vtm_ ## type a2) \
	{								\
		vtm_dur r;						\
		r.d = a1.vtm_ ## type ## _memb - a2.vtm_ ## type ## _memb; \
		return (r);						\
	}

#define vtm_cmp(type, name, op)						\
	static inline int						\
	VTM_ ## type ## _ ## name (const vtm_ ## type a1,		\
	    const vtm_ ## type a2)					\
	{								\
		return a1.vtm_ ## type ## _memb op a2.vtm_ ## type ## _memb; \
	}

vtm_dur_op(mono, add, +)
vtm_dur_op(mono, sub, -)
vtm_dur_op(real, add, +)
vtm_dur_op(real, sub, -)

vtm_diff(mono)
vtm_diff(real)

vtm_cmp(mono, eq, ==)
vtm_cmp(mono, ne, !=)
vtm_cmp(mono, gt, >)
vtm_cmp(mono, lt, <)
vtm_cmp(mono, ge, >=)
vtm_cmp(mono, le, <=)

vtm_cmp(real, eq, ==)
vtm_cmp(real, ne, !=)
vtm_cmp(real, gt, >)
vtm_cmp(real, lt, <)
vtm_cmp(real, ge, >=)
vtm_cmp(real, le, <=)

vtm_cmp(dur, eq, ==)
vtm_cmp(dur, ne, !=)
vtm_cmp(dur, gt, >)
vtm_cmp(dur, lt, <)
vtm_cmp(dur, ge, >=)
vtm_cmp(dur, le, <=)

#undef vtm_diff
#undef vtm_dur_op
#undef vtm_dur_memb
#undef vtm_mono_memb
#undef vtm_real_memb
