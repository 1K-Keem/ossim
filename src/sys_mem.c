/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "os-mm.h"
#include "syscall.h"
#include "libmem.h"
#define OSSIM_PROJECT_SCHED_H
#include "sched.h"
#undef OSSIM_PROJECT_SCHED_H
#include "log.h"

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

//typedef char BYTE;

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs* regs)
{
   int memop = regs->a1;
   int ret = 0;
   BYTE value;
   struct pcb_t *caller = find_proc(pid);

   /*
    * @bksysnet: Please note in the dual spacing design
    *            syscall implementations are in kernel space.
    */

   if (krnl == NULL || caller == NULL) {
            os_log(LOG_ERROR, "sys_mem", "pid=%u not found for memop=%d", pid, memop);
            return -1;
   }

   os_log(LOG_DEBUG, "sys_mem", "pid=%u memop=%d", pid, memop);
   switch (memop) {
   case SYSMEM_MAP_OP:
            /* Reserved process case*/
			ret = vmap_pgd_memset(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_INC_OP:
            ret = inc_vma_limit(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_SWP_OP:
            ret = __mm_swap_page(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_IO_READ:
            if (MEMPHY_read(caller->krnl->mram, regs->a2, &value) < 0) {
                     os_log(LOG_ERROR, "sys_mem", "pid=%u read failed addr=" FORMAT_ADDR, pid, regs->a2);
                     return -1;
            }
            regs->a3 = value;
            break;
   case SYSMEM_IO_WRITE:
            if (MEMPHY_write(caller->krnl->mram, regs->a2, regs->a3) < 0) {
                     os_log(LOG_ERROR, "sys_mem", "pid=%u write failed addr=" FORMAT_ADDR, pid, regs->a2);
                     return -1;
            }
            break;
   default:
            os_log(LOG_WARN, "sys_mem", "pid=%u unknown memop=%d", pid, memop);
            return -1;
   }

   if (ret < 0) {
            os_log(LOG_ERROR, "sys_mem", "pid=%u memop=%d failed", pid, memop);
            return -1;
   }
   
   return 0;
}
