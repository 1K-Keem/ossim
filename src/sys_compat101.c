/*
 * Compatibility syscall for instructor-provided sample input.
 * It intentionally performs no stdout-visible work.
 */

#include "common.h"
#include "syscall.h"

int __sys_compat101(struct krnl_t *krnl, uint32_t pid, struct sc_regs *regs)
{
   (void)krnl;
   (void)pid;
   (void)regs;

   return 0;
}
