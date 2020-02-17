/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2011 Poul-Henning Kamp
 * Copyright (c) 2000-2008 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      $FreeBSD: head/sys/sys/vsb.h 221993 2011-05-16 16:18:40Z phk $
 */

#ifndef VSB_H_INCLUDED
#define VSB_H_INCLUDED

#include <stdlib.h>
#include <string.h>

/*
 * Structure definition
 */
struct vsb {
	unsigned	magic;
#define VSB_MAGIC	0x4a82dd8a
	int		 s_error;	/* current error code */
	char		*s_buf;		/* storage buffer */
	ssize_t		 s_size;	/* size of storage buffer */
	ssize_t		 s_len;		/* current length of string */
#define	VSB_FIXEDLEN	0x00000000	/* fixed length buffer (default) */
#define	VSB_AUTOEXTEND	0x00000001	/* automatically extend buffer */
#define	VSB_USRFLAGMSK	0x0000ffff	/* mask of flags the user may specify */
#define	VSB_DYNAMIC	0x00010000	/* s_buf must be freed */
#define	VSB_FINISHED	0x00020000	/* set by VSB_finish() */
#define	VSB_DYNSTRUCT	0x00080000	/* vsb must be freed */
	int		 s_flags;	/* flags */
	int		 s_indent;	/* Ident level */
};

#ifdef __cplusplus
extern "C" {
#endif

#define	KASSERT(e, m)		assert(e)
#define	SBMALLOC(size)		malloc(size)
#define	SBFREE(buf)		free(buf)

#define	roundup2(x, y)	(((x)+((y)-1))&(~((y)-1))) /* if y is powers of two */

/*
 * Predicates
 */
#define	VSB_ISDYNAMIC(s)	((s)->s_flags & VSB_DYNAMIC)
#define	VSB_ISDYNSTRUCT(s)	((s)->s_flags & VSB_DYNSTRUCT)
#define	VSB_HASROOM(s)		((s)->s_len < (s)->s_size - 1L)
#define	VSB_FREESPACE(s)	((s)->s_size - ((s)->s_len + 1L))
#define	VSB_CANEXTEND(s)	((s)->s_flags & VSB_AUTOEXTEND)

/*
 * Set / clear flags
 */
#define	VSB_SETFLAG(s, f)	do { (s)->s_flags |= (f); } while (0)
#define	VSB_CLEARFLAG(s, f)	do { (s)->s_flags &= ~(f); } while (0)

#define	VSB_MINEXTENDSIZE	16		/* Should be power of 2. */

#ifdef PAGE_SIZE
#define	VSB_MAXEXTENDSIZE	PAGE_SIZE
#define	VSB_MAXEXTENDINCR	PAGE_SIZE
#else
#define	VSB_MAXEXTENDSIZE	4096
#define	VSB_MAXEXTENDINCR	4096
#endif

/*
 * Debugging support
 */
#if !defined(NDEBUG)
static inline void
_assert_VSB_integrity(const char *fun, const struct vsb *s)
{

	(void)fun;
	(void)s;
	KASSERT(s != NULL,
	    ("%s called with a NULL vsb pointer", fun));
	KASSERT(s->magic == VSB_MAGIC,
	    ("%s called wih an bogus vsb pointer", fun));
	KASSERT(s->s_buf != NULL,
	    ("%s called with uninitialized or corrupt vsb", fun));
	KASSERT(s->s_len < s->s_size,
	    ("wrote past end of vsb (%d >= %d)", s->s_len, s->s_size));
}

static inline void
_assert_VSB_state(const char *fun, const struct vsb *s, int state)
{

	(void)fun;
	(void)s;
	(void)state;
	KASSERT((s->s_flags & VSB_FINISHED) == state,
	    ("%s called with %sfinished or corrupt vsb", fun,
	    (state ? "un" : "")));
}
#define	assert_VSB_integrity(s) _assert_VSB_integrity(__func__, (s))
#define	assert_VSB_state(s, i)	 _assert_VSB_state(__func__, (s), (i))
#else
#define	assert_VSB_integrity(s) do { } while (0)
#define	assert_VSB_state(s, i)	 do { } while (0)
#endif

#ifdef CTASSERT
CTASSERT(powerof2(VSB_MAXEXTENDSIZE));
CTASSERT(powerof2(VSB_MAXEXTENDINCR));
#endif

static inline ssize_t
VSB_extendsize(ssize_t size)
{
	ssize_t newsize;

	if (size < (int)VSB_MAXEXTENDSIZE) {
		newsize = VSB_MINEXTENDSIZE;
		while (newsize < size)
			newsize *= 2;
	} else {
		newsize = roundup2(size, VSB_MAXEXTENDINCR);
	}
	KASSERT(newsize >= size, ("%s: %d < %d\n", __func__, newsize, size));
	return (newsize);
}

/*
 * Extend an vsb.
 */
static inline ssize_t
VSB_extend(struct vsb *s, ssize_t addlen)
{
	char *newbuf;
	ssize_t newsize;

	if (!VSB_CANEXTEND(s))
		return (-1);
	newsize = VSB_extendsize(s->s_size + addlen);
	if (VSB_ISDYNAMIC(s))
		newbuf = realloc(s->s_buf, newsize);
	else
		newbuf = SBMALLOC(newsize);
	if (newbuf == NULL)
		return (-1);
	if (!VSB_ISDYNAMIC(s)) {
		memcpy(newbuf, s->s_buf, s->s_size);
		VSB_SETFLAG(s, VSB_DYNAMIC);
	}
	s->s_buf = newbuf;
	s->s_size = newsize;
	return (0);
}

static inline void
_vsb_indent(struct vsb *s)
{
	if (s->s_indent == 0 || s->s_error != 0 ||
	    (s->s_len > 0 && s->s_buf[s->s_len - 1] != '\n'))
		return;
	if (VSB_FREESPACE(s) <= s->s_indent &&
	    VSB_extend(s, s->s_indent) < 0) {
		s->s_error = ENOMEM;
		return;
	}
	memset(s->s_buf + s->s_len, ' ', s->s_indent);
	s->s_len += s->s_indent;
}


/*
 * Append a byte to an vsb.  This is the core function for appending
 * to an vsb and is the main place that deals with extending the
 * buffer and marking overflow.
 */
static inline void
VSB_put_byte(struct vsb *s, int c)
{

	assert_VSB_integrity(s);
	assert_VSB_state(s, 0);

	if (s->s_error != 0)
		return;
	_vsb_indent(s);
	if (VSB_FREESPACE(s) <= 0) {
		if (VSB_extend(s, 1) < 0)
			s->s_error = ENOMEM;
		if (s->s_error != 0)
			return;
	}
	s->s_buf[s->s_len++] = (char)c;
}

/*
 * Append a character to an vsb.
 */
static inline int
VSB_putc(struct vsb *s, int c)
{

	VSB_put_byte(s, c);
	if (s->s_error != 0)
		return (-1);
	return (0);
}


/*
 * API functions
 */
struct vsb	*VSB_new(struct vsb *, char *, int, int);
#define		 VSB_new_auto()				\
	VSB_new(NULL, NULL, 0, VSB_AUTOEXTEND)
void		 VSB_clear(struct vsb *);
int		 VSB_bcat(struct vsb *, const void *, ssize_t);
int		 VSB_cat(struct vsb *, const char *);
int		 VSB_printf(struct vsb *, const char *, ...)
	v_printflike_(2, 3);
#ifdef va_start
int		 VSB_vprintf(struct vsb *, const char *, va_list)
	v_printflike_(2, 0);
#endif
int		 VSB_error(const struct vsb *);
int		 VSB_finish(struct vsb *);
char		*VSB_data(const struct vsb *);
ssize_t		 VSB_len(const struct vsb *);
void		 VSB_delete(struct vsb *);
void		 VSB_destroy(struct vsb **);
#define VSB_QUOTE_NONL		1
#define VSB_QUOTE_JSON		2
#define VSB_QUOTE_HEX		4
#define VSB_QUOTE_CSTR		8
#define VSB_QUOTE_UNSAFE	16
#define VSB_QUOTE_ESCHEX	32
void		 VSB_quote_pfx(struct vsb *, const char*, const void *,
		     int len, int how);
void		 VSB_quote(struct vsb *, const void *, int len, int how);
void		 VSB_indent(struct vsb *, int);
int		 VSB_tofile(int fd, const struct vsb *);
#ifdef __cplusplus
};
#endif

#endif
