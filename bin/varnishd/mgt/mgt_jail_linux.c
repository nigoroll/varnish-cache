/*-
 * Copyright (c) 2006-2020 Varnish Software AS
 * All rights reserved.
 *
 * Author: Marco Benatto <mbenatto@redhat.com>
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
 * Restricts the syscalls only to necessary ones using seccomp and still lower
 * privilleges by using setuid() as done on UNIX jail.
 *
 * The setup is pretty much the same as on UNIX jail, however besides drop
 * privilleges we also block any syscall we don't really use using seccomp(2).
 * If, eventually, someone manage to inject code or trick the mgt ou cld the
 * one won't be able to execute any locked syscall (let's say make someone
 * exec() arbitrary code)
 */


#ifdef __linux__

#include "config.h"
#include "mgt/mgt.h"
#include "common/heritage.h"
#include "mgt_jail_linux.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <seccomp.h>

static void vjl_sig_handler(int sig, siginfo_t *si, void *unused)
{
	/* TODO: need to find a way to use SYS_SECCOMP otherwise we intercept all SIGSYS*/
	if (sig == SIGSYS) {
		printf("[%u]syscall: %d not permitted at: %p\n", si->si_pid, 
			si->si_syscall, si->si_call_addr);
	}
	(void)unused;
}

static int vjl_seccomp_init() {

	return 0;
}

static void v_matchproto_(jail_subproc_f)
vjl_subproc(enum jail_subproc_e jse) {

	if (jse == JAIL_SUBPROC_WORKER) {
		scmp_filter_ctx ctx;
		struct sigaction sa;

		sa.sa_flags = SA_SIGINFO;
		sigemptyset(&sa.sa_mask);
		sa.sa_sigaction = vjl_sig_handler;
		AZ(sigaction(SIGSYS, &sa, NULL));

		ctx = seccomp_init(SCMP_ACT_TRAP);

		AN(ctx);

		//seccomp_rule_add(ctx, SCMP_ACT_TRAP, SCMP_SYS(fork), 0);

		seccomp_load(ctx);

		seccomp_release(ctx);
		(void)jse;
	}
}


static void v_matchproto_(jail_master_f)
vjl_master(enum jail_master_e jme)
{
	(void)jme;
}

static int v_matchproto_(jail_init_f)
vjl_init(char **args)
{
	if (geteuid() != 0 )
		ARGV_ERR("Linux Jail: Must be root.\n");

	vjl_seccomp_init();

	(void)args;
	return 0;

	/* We follow the same parameters from UNIX jail here */
}

const struct jail_tech jail_tech_linux = {
	.magic =	JAIL_TECH_MAGIC,
	.name =		"linux",
	.init =		vjl_init,
	.master =	vjl_master,
	.subproc =	vjl_subproc,
};
#else
const struct jail_tech jail_tech_linux = {
	.magic =	JAIL_TECH_MAGIC,
	.name =		"linux",
	.init =		NULL,
};
#endif
