/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "syscall.h"
#include "common.h"
#include "log.h"

#define __SYSCALL(nr, sym) extern int __##sym(struct krnl_t*, uint32_t,struct sc_regs*);
#include "syscalltbl.lst"
#undef  __SYSCALL

/*
 * The sys_call_table[] is used for system calls, but to know the system
 * call address.
 */
#define __SYSCALL(nr, sym) #nr "-" #sym,
const char* sys_call_table[] = {
#include "syscalltbl.lst"
};
#undef  __SYSCALL
const int syscall_table_size = sizeof(sys_call_table)/sizeof(char*);

int __sys_ni_syscall(struct krnl_t *krnl, struct sc_regs *regs)
{
   /*
    * DUMMY systemcall
    */

	os_log(LOG_WARN, "syscall", "unimplemented syscall invoked");
	return -1;
}

int _syscall(struct krnl_t *krnl, uint32_t pid, uint32_t nr, struct sc_regs* regs)
{
	int ret;

	os_log(LOG_DEBUG, "syscall", "enter pid=%u nr=%u", pid, nr);
	switch (nr) {
	#define __SYSCALL(nr, sym) case nr: ret = __##sym(krnl,pid,regs); break;
	#include "syscalltbl.lst"
	#undef __SYSCALL
	default: ret = __sys_ni_syscall(krnl, regs); break;
	}
	os_log(LOG_DEBUG, "syscall", "exit pid=%u nr=%u ret=%d", pid, nr, ret);
	return ret;
};
