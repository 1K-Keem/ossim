/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c 
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include "log.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef MM64
#define KMEM_PAGE_SIZE PAGING64_PAGESZ
#define KMEM_PAGE_ALIGN(sz) PAGING64_PAGE_ALIGNSZ(sz)
#define KMEM_VADDR_BASE 0xffff800000000000ULL
#else
#define KMEM_PAGE_SIZE PAGING_PAGESZ
#define KMEM_PAGE_ALIGN(sz) PAGING_PAGE_ALIGNSZ(sz)
#define KMEM_VADDR_BASE 0
#endif

#ifdef MM64
static int is_kernel_vaddr_range(addr_t start, addr_t end);
static int is_user_vaddr_range(addr_t start, addr_t end);
#endif
static addr_t *kernel_walk_pte(struct krnl_t *krnl, addr_t addr, int create);

static void trace_print_addr(addr_t addr)
{
  printf("0x%llx", (unsigned long long)addr);
}

static int trace_proc_has_kernel_ops(struct pcb_t *proc)
{
  if (proc == NULL || proc->code == NULL || proc->code->text == NULL)
    return 0;

  for (uint32_t idx = 0; idx < proc->code->size; idx++)
  {
    enum ins_opcode_t opcode = proc->code->text[idx].opcode;
    if (opcode == KMALLOC || opcode == KMEM_CACHE_CREATE ||
        opcode == KMEM_CACHE_ALLOC || opcode == COPY_FROM_USER ||
        opcode == COPY_TO_USER)
      return 1;
  }

  return 0;
}

static int trace_has_kernel_state(struct pcb_t *proc)
{
  struct mm_struct *mm = NULL;

  if (trace_proc_has_kernel_ops(proc))
    return 1;

  if (proc == NULL || proc->krnl == NULL || proc->krnl->mm == NULL)
    return 0;

  mm = proc->krnl->mm;
  for (int idx = 0; idx < PAGING_MAX_SYMTBL_SZ; idx++)
  {
    struct vm_rg_struct *rg = &mm->symrgtbl[idx];
    if (rg->rg_end > rg->rg_start && rg->rg_start >= KMEM_VADDR_BASE)
      return 1;
  }

  return mm->kcpooltbl != NULL && mm->kcpooltbl_size > 0;
}

static int trace_frame_is_free(struct memphy_struct *mp, addr_t fpn)
{
  struct framephy_struct *fp = mp ? mp->free_fp_list : NULL;

  while (fp != NULL)
  {
    if (fp->fpn == fpn)
      return 1;
    fp = fp->fp_next;
  }

  return 0;
}

static void trace_print_frame_list(struct framephy_struct *fp)
{
  if (fp == NULL)
  {
    printf("(empty)");
    return;
  }

  while (fp != NULL)
  {
    printf("[%llu] ", (unsigned long long)fp->fpn);
    fp = fp->fp_next;
  }
}

static void trace_print_used_frames(struct memphy_struct *mp)
{
  int total_frames;
  int printed = 0;

  if (mp == NULL || mp->maxsz <= 0)
  {
    printf("(empty)");
    return;
  }

  total_frames = mp->maxsz / KMEM_PAGE_SIZE;
  for (int fpn = total_frames - 1; fpn >= 0; fpn--)
  {
    if (!trace_frame_is_free(mp, (addr_t)fpn))
    {
      printf("[%d] ", fpn);
      printed = 1;
    }
  }

  if (!printed)
    printf("(empty)");
}

static void trace_dump_memphy(const char *label, struct memphy_struct *mp)
{
  int total_frames;

  if (label == NULL || mp == NULL || mp->storage == NULL)
    return;

  total_frames = mp->maxsz / KMEM_PAGE_SIZE;
  printf("%s | size:%d\n", label, mp->maxsz);
  printf("STORAGE\n");

  pthread_mutex_lock(&mp->lock);
  for (int fpn = 0; fpn < total_frames; fpn++)
  {
    addr_t base = (addr_t)fpn * KMEM_PAGE_SIZE;

    printf("[PAGE %d] : ", fpn);
    for (int word = 0; word < 16 && base + (addr_t)word * 4 < (addr_t)mp->maxsz; word++)
      printf("%08x ", (unsigned char)mp->storage[base + (addr_t)word * 4]);
    printf("\n");
  }

  printf("FREE frames: ");
  trace_print_frame_list(mp->free_fp_list);
  printf("\n");
  printf("USED frames: ");
  trace_print_used_frames(mp);
  printf("\n");
  pthread_mutex_unlock(&mp->lock);
  printf("======================\n");
}

static void trace_dump_kernel_pgtbl(struct pcb_t *proc)
{
  if (proc == NULL || proc->krnl == NULL)
    return;

  printf("PAGE TABLE KENEL: ");
  for (int idx = 0; idx < 4; idx++)
  {
    addr_t addr = KMEM_VADDR_BASE + (addr_t)idx * KMEM_PAGE_SIZE;
    addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;
    addr_t *pte = kernel_walk_pte(proc->krnl, addr, 0);

    printf("[page=%llx -> ", (unsigned long long)pgn);
    if (pte != NULL && PAGING64_PAGE_PRESENT(*pte))
      printf("frame=%llu", (unsigned long long)PAGING64_PTE_FPN(*pte));
    else
      printf("none");
    printf("] ");
  }
  printf("\n======================\n");
}

static void trace_dump_user_pgtbl(struct pcb_t *proc)
{
  printf("PAGE TABLE: ");
  for (addr_t pgn = 0; pgn < 6; pgn++)
  {
    addr_t pte = pte_get_entry(proc, pgn);

    printf("[page=%llu -> ", (unsigned long long)pgn);
    if (PAGING64_PAGE_PRESENT(pte))
      printf("frame=%llu", (unsigned long long)PAGING64_PTE_FPN(pte));
    else if (PAGING64_PAGE_SWAPPED(pte))
      printf("swap=%llu", (unsigned long long)PAGING64_PTE_SWP(pte));
    else
      printf("none");
    printf("] ");
  }
  printf("\n");
}

static void trace_dump_vma(struct mm_struct *mm)
{
  struct vm_area_struct *vma = mm ? mm->mmap : NULL;
  addr_t traced_sbrk = 0;
  int printed = 0;

  if (vma == NULL)
  {
    printf("VMA | id=0 | start=0 | end=0 | sbrk=0 | free regions: [0-0]\n");
    return;
  }

  if (mm != NULL)
  {
    for (int idx = 0; idx < PAGING_MAX_SYMTBL_SZ; idx++)
    {
      struct vm_rg_struct *rg = &mm->symrgtbl[idx];
      if (rg->rg_end > rg->rg_start && rg->rg_start < KMEM_VADDR_BASE &&
          traced_sbrk < rg->rg_end)
        traced_sbrk = rg->rg_end;
    }
  }

  printf("VMA | id=%lu | start=%llu | end=%llu | sbrk=%llu | free regions: ",
         vma->vm_id,
         (unsigned long long)vma->vm_start,
         (unsigned long long)vma->vm_end,
         (unsigned long long)traced_sbrk);

  struct vm_rg_struct *rg = vma->vm_freerg_list;
  while (rg != NULL)
  {
    if (rg->rg_end > rg->rg_start)
    {
      if (printed)
        printf(" -> ");
      printf("[%llu-%llu]", (unsigned long long)rg->rg_start,
             (unsigned long long)rg->rg_end);
      printed = 1;
    }
    rg = rg->rg_next;
  }

  if (!printed)
    printf("[%llu-%llu]", (unsigned long long)traced_sbrk,
           (unsigned long long)vma->vm_end);

  printf("\n");
}

static void trace_dump_symbol_table(struct mm_struct *mm, int show_mode)
{
  printf("SYMBOL TABLE: ");
  if (mm == NULL)
  {
    printf("(empty)\n");
    return;
  }

  int printed = 0;
  for (int idx = 0; idx < PAGING_MAX_SYMTBL_SZ; idx++)
  {
    struct vm_rg_struct *rg = &mm->symrgtbl[idx];

    if (rg->rg_end <= rg->rg_start)
      continue;

    if (show_mode)
    {
      printf("[rg%d:", idx);
      trace_print_addr(rg->rg_start);
      printf("-");
      trace_print_addr(rg->rg_end);
      printf(" mode=%s] ", rg->rg_start >= KMEM_VADDR_BASE ? "KERNEL" : "USER");
    }
    else
    {
      printf("[rg%d:%llu-%llu] ", idx,
             (unsigned long long)rg->rg_start,
             (unsigned long long)rg->rg_end);
    }
    printed = 1;
  }

  if (!printed)
    printf("(empty)");
  printf("\n");
}

static void trace_dump_fifo(struct mm_struct *mm)
{
  struct pgn_t *pg = mm ? mm->fifo_pgn : NULL;

  printf("FIFO PAGE LIST: ");
  if (pg == NULL)
  {
    printf("(empty)\n");
    return;
  }

  while (pg != NULL)
  {
    printf("[%llu]", (unsigned long long)pg->pgn);
    if (pg->pg_next != NULL)
      printf(" -> ");
    pg = pg->pg_next;
  }
  printf("\n");
}

static void trace_dump_cache_pools(struct mm_struct *mm)
{
  printf("KMEM CACHE POOLS:\n");
  if (mm == NULL || mm->kcpooltbl == NULL)
  {
    printf("\n");
    return;
  }

  for (int idx = 0; idx < mm->kcpooltbl_size; idx++)
  {
    struct kcache_pool_struct *pool = &mm->kcpooltbl[idx];

    if (pool->size == 0)
      continue;

    printf("  pool[%d] size=%d align=%d storage=", idx, pool->size, pool->align);
    trace_print_addr(pool->storage);
    printf("\n");
  }
  printf("\n");
}

static void trace_dump_state(struct pcb_t *proc, const char *op,
                          arg_t a0, arg_t a1, arg_t a2, arg_t a3)
{
  int show_kernel = trace_has_kernel_state(proc);
  struct mm_struct *mm = NULL;

  if (proc == NULL || proc->krnl == NULL || proc->krnl->mm == NULL)
    return;

  mm = proc->krnl->mm;

  printf("\n======================================== KERNEL INFO ========================================\n");
  printf("[EXEC] %s | args=(" FORMAT_ARG "," FORMAT_ARG "," FORMAT_ARG "," FORMAT_ARG ")\n",
         op, a0, a1, a2, a3);
  trace_dump_memphy("RAM", proc->krnl->mram);
  trace_dump_memphy("ACTIVE SWAP", proc->krnl->active_mswp);
  if (show_kernel)
    trace_dump_kernel_pgtbl(proc);
  printf("MM STRUCT\n");
  trace_dump_user_pgtbl(proc);
  trace_dump_vma(mm);
  trace_dump_symbol_table(mm, show_kernel);
  trace_dump_fifo(mm);
  if (show_kernel)
    trace_dump_cache_pools(mm);
  printf("================================ END ================================\n\n");
}

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (mm == NULL || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL ||
      rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ || size == 0 || alloc_addr == NULL)
    return -1;

  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (cur_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* Fast path: find a free region in the existing free list */
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->krnl->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->krnl->mm->symrgtbl[rgid].rg_end   = rgnode.rg_end;
    caller->krnl->mm->symrgtbl[rgid].vmaid     = vmaid;

    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* Slow path: free list exhausted — expand the VMA via SYSCALL memmap INC */

  /* Compute page-aligned byte increment large enough to hold 'size' bytes */
#ifdef MM64
  addr_t inc_amt = PAGING64_PAGE_ALIGNSZ(size);
#else
  addr_t inc_amt = PAGING_PAGE_ALIGNSZ(size);
#endif

  /* Unlock before the syscall: inc_vma_limit internally acquires its own locks
   * and calls vm_map_ram which may also contend — avoid nested lock. */
  pthread_mutex_unlock(&mmvm_lock);

  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
  regs.a3 = inc_amt;
  int sret = _syscall(caller->krnl, caller->pid, 17, &regs); /* sys_memmap */

  pthread_mutex_lock(&mmvm_lock);

  if (sret < 0) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1; /* VMA expansion failed */
  }

  /* After expansion the newly mapped range is in the VMA free list.
   * Re-fetch to get the actual mapped region for this allocation. */
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) != 0) {
    /* Should not happen if inc_vma_limit succeeded, but guard anyway */
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  caller->krnl->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
  caller->krnl->mm->symrgtbl[rgid].rg_end   = rgnode.rg_end;
  caller->krnl->mm->symrgtbl[rgid].vmaid     = vmaid;

  *alloc_addr = rgnode.rg_start;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;

  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->krnl->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  if (freerg_node == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_vma == NULL) {
    free(freerg_node);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  enlist_vm_rg_node(&cur_vma->vm_freerg_list, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  if (trace_proc_has_kernel_ops(proc))
    printf("liballoc:227 pid=%u pc=%u alloc reg=%u size=" FORMAT_ADDR " addr=0x%llx\n",
           proc->pid, proc->pc, reg_index, size, (unsigned long long)addr);
  trace_dump_state(proc, "ALLOC", size, reg_index, 0, 0);
#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    os_log(LOG_WARN, "libmem", "pid=%u free failed rgid=%u", proc->pid, reg_index);
    return -1;
  }
#ifdef IODUMP
  if (trace_proc_has_kernel_ops(proc))
    printf("libfree:260 pid=%u pc=%u free reg=%u\n",
           proc->pid, proc->pc, reg_index);
  trace_dump_state(proc, "FREE", reg_index, 0, 0, 0);
#endif
  return 0;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, addr_t pgn, addr_t *fpn, struct pcb_t *caller)
{

  addr_t pte = pte_get_entry(caller, pgn);

#ifdef MM64
  if (!PAGING64_PAGE_PRESENT(pte))
  {
    if (!PAGING64_PAGE_SWAPPED(pte))
      return -1;

    addr_t tgtfpn;
    addr_t req_swpoff = PAGING64_PTE_SWP(pte);

    if (swap_out_victim_page(caller, &tgtfpn) != 0)
      return -1;

    if (__swap_cp_page(caller->krnl->active_mswp, req_swpoff,
                       caller->krnl->mram, tgtfpn) != 0)
      return -1;

    MEMPHY_put_freefp(caller->krnl->active_mswp, req_swpoff);
    pte_set_fpn(caller, pgn, tgtfpn);

    enlist_pgn_node(&mm->fifo_pgn, pgn);
  }

  *fpn = PAGING64_PTE_FPN(pte_get_entry(caller, pgn));
#else
  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, bring it into RAM via swap */
    if ((pte & PAGING_PTE_SWAPPED_MASK) == 0)
      return -1;

    addr_t tgtfpn;

    /* Remember the swap offset of the requested page (if it was previously swapped out) */
    addr_t req_swpoff = PAGING_SWP(pte);

    if (swap_out_victim_page(caller, &tgtfpn) != 0)
      return -1;

    if (__swap_cp_page(caller->krnl->active_mswp, req_swpoff,
                       caller->krnl->mram, tgtfpn) != 0)
      return -1;

    MEMPHY_put_freefp(caller->krnl->active_mswp, req_swpoff);
    pte_set_fpn(caller, pgn, tgtfpn);

    /* Enlist back to FIFO queue to track for future eviction and cleanup */
    enlist_pgn_node(&mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(caller, pgn));
#endif

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, addr_t addr, BYTE *data, struct pcb_t *caller)
{
  addr_t pgn;
  addr_t off;
  addr_t fpn;

#ifdef MM64
  pgn = addr >> PAGING64_ADDR_PT_SHIFT;
  off = addr & PAGING64_ADDR_OFFST_MASK;
#else
  pgn = PAGING_PGN(addr);
  off = PAGING_OFFST(addr);
#endif

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  /* Compute physical address: frame base + page offset */
#ifdef MM64
  addr_t phyaddr = (addr_t)fpn * PAGING64_PAGESZ + off;
#else
  addr_t phyaddr = (addr_t)(fpn << PAGING_ADDR_FPN_LOBIT) + off;
#endif

  /* Read directly from physical RAM */
  if (MEMPHY_read(caller->krnl->mram, phyaddr, data) < 0)
    return -1;

  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, addr_t addr, BYTE value, struct pcb_t *caller)
{
  addr_t pgn;
  addr_t off;
  addr_t fpn;

#ifdef MM64
  pgn = addr >> PAGING64_ADDR_PT_SHIFT;
  off = addr & PAGING64_ADDR_OFFST_MASK;
#else
  pgn = PAGING_PGN(addr);
  off = PAGING_OFFST(addr);
#endif

  /* Get the page into RAM, swapping in from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  /* Compute physical address: frame base + page offset */
#ifdef MM64
  addr_t phyaddr = (addr_t)fpn * PAGING64_PAGESZ + off;
#else
  addr_t phyaddr = (addr_t)(fpn << PAGING_ADDR_FPN_LOBIT) + off;
#endif

  /* Write directly to physical RAM */
  if (MEMPHY_write(caller->krnl->mram, phyaddr, value) < 0)
    return -1;

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

//struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  /* TODO Invalid memory identify */
  if (currg == NULL || currg->rg_start == currg->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    os_log(LOG_WARN, "libmem", "pid=%u invalid read rgid=%d", caller->pid, rgid);
    return -1;
  }

  if (offset >= (currg->rg_end - currg->rg_start)) {
    pthread_mutex_unlock(&mmvm_lock);
    os_log(LOG_WARN, "libmem", "pid=%u invalid read offset=" FORMAT_ARG " rgid=%d", caller->pid, offset, rgid);
    return -1;
  }

  addr_t access_addr = currg->rg_start + offset;
  if (access_addr < currg->rg_start
#ifdef MM64
      || !is_user_vaddr_range(currg->rg_start, currg->rg_end)
      || !is_user_vaddr_range(access_addr, access_addr + 1)
#endif
     ) {
    pthread_mutex_unlock(&mmvm_lock);
    os_log(LOG_WARN, "libmem", "pid=%u rejected kernel/non-canonical read rgid=%d", caller->pid, rgid);
    return -1;
  }

  int ret = pg_getval(caller->krnl->mm, access_addr, data, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return ret;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t destination) // BUG 4 Fix: Change to value instead of pointer
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  if (val == -1)
    return -1;

  proc->regs[destination] = data; // BUG 4 Fix: write to process register
#ifdef IODUMP
  if (trace_proc_has_kernel_ops(proc))
    printf("libread:441 pid=%u pc=%u read reg=%u offset=" FORMAT_ADDR " data=%u dest=" FORMAT_ADDR "\n",
           proc->pid, proc->pc, source, offset, (unsigned int)data,
           proc->regs[destination]);
  trace_dump_state(proc, "READ", source, offset, proc->regs[destination], 0);
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->krnl->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (currg == NULL || cur_vma == NULL || currg->rg_start == currg->rg_end) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (offset >= (currg->rg_end - currg->rg_start)) {
    pthread_mutex_unlock(&mmvm_lock);
    os_log(LOG_WARN, "libmem", "pid=%u invalid write offset=" FORMAT_ARG " rgid=%d", caller->pid, offset, rgid);
    return -1;
  }

  addr_t access_addr = currg->rg_start + offset;
  if (access_addr < currg->rg_start
#ifdef MM64
      || !is_user_vaddr_range(currg->rg_start, currg->rg_end)
      || !is_user_vaddr_range(access_addr, access_addr + 1)
#endif
     ) {
    pthread_mutex_unlock(&mmvm_lock);
    os_log(LOG_WARN, "libmem", "pid=%u rejected kernel/non-canonical write rgid=%d", caller->pid, rgid);
    return -1;
  }

  int ret = pg_setval(caller->krnl->mm, access_addr, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return ret;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  if (trace_proc_has_kernel_ops(proc))
    printf("libwrite:499 pid=%u pc=%u write data=%u reg=%u offset=" FORMAT_ADDR "\n",
           proc->pid, proc->pc, (unsigned int)data, destination, offset);
  trace_dump_state(proc, "WRITE", data, destination, offset, 0);
#endif

  return val;
}


static int framephy_compare(const void *a, const void *b)
{
  // Compare function for qsort to sort frame nodes by frame number
  // Used to find contiguous frame sequences

  const struct framephy_struct *fa = *(const struct framephy_struct * const *)a;
  const struct framephy_struct *fb = *(const struct framephy_struct * const *)b;

  if (fa->fpn < fb->fpn)
    return -1;
  if (fa->fpn > fb->fpn)
    return 1;
  return 0;
}

static struct framephy_struct *kernel_get_free_frame_node(struct memphy_struct *mp, struct framephy_struct *node)
{
  // Remove a specific frame node from the free frame list
  // Returns the node if found, NULL otherwise

  struct framephy_struct *prev = NULL;
  struct framephy_struct *curr = mp->free_fp_list;

  while (curr != NULL)
  {
    if (curr == node)
    {
      if (prev != NULL)
        prev->fp_next = curr->fp_next;
      else
        mp->free_fp_list = curr->fp_next;
      return curr;
    }
    prev = curr;
    curr = curr->fp_next;
  }

  return NULL;
}

static struct framephy_struct *kernel_get_used_frame_node(struct memphy_struct *mp, addr_t fpn)
{
  // Remove a specific frame node from the used frame list
  // Returns the node if found, NULL otherwise

  struct framephy_struct *prev = NULL;
  struct framephy_struct *curr = mp->used_fp_list;

  while (curr != NULL)
  {
    if (curr->fpn == fpn)
    {
      if (prev != NULL)
        prev->fp_next = curr->fp_next;
      else
        mp->used_fp_list = curr->fp_next;
      return curr;
    }
    prev = curr;
    curr = curr->fp_next;
  }

  return NULL;
}

static int kernel_allocate_contiguous_frames(struct pcb_t *caller, int req_pgnum, addr_t *start_fpn)
{
  // Allocate a contiguous block of physical frames for kernel memory
  // Finds consecutive free frames and marks them as used

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mram == NULL)
    return -1;

  struct memphy_struct *mram = caller->krnl->mram;
  struct framephy_struct *fp;
  int count = 0;
  struct framephy_struct **list = NULL;
  int select_start = -1;
  int idx;

  if (req_pgnum <= 0 || start_fpn == NULL)
    return -1;

  pthread_mutex_lock(&mram->lock);

  for (fp = mram->free_fp_list; fp != NULL; fp = fp->fp_next)
    count++;

  if (count < req_pgnum)
  {
    pthread_mutex_unlock(&mram->lock);
    return -1;
  }

  list = malloc(sizeof(struct framephy_struct *) * count);
  if (list == NULL)
  {
    pthread_mutex_unlock(&mram->lock);
    return -1;
  }

  idx = 0;
  for (fp = mram->free_fp_list; fp != NULL; fp = fp->fp_next)
    list[idx++] = fp;

  qsort(list, count, sizeof(*list), framephy_compare);

  if (req_pgnum == 1)
  {
    select_start = 0;
  }
  else
  {
    int run = 1;
    for (idx = 1; idx < count; idx++)
    {
      if (list[idx]->fpn == list[idx - 1]->fpn + 1)
      {
        run++;
      }
      else
      {
        run = 1;
      }

      if (run == req_pgnum)
      {
        select_start = idx - req_pgnum + 1;
        break;
      }
    }
  }

  if (select_start == -1)
  {
    free(list);
    pthread_mutex_unlock(&mram->lock);
    return -1;
  }

  *start_fpn = list[select_start]->fpn;

  for (idx = select_start; idx < select_start + req_pgnum; idx++)
  {
    struct framephy_struct *node = kernel_get_free_frame_node(mram, list[idx]);
    if (node == NULL)
      continue;

    node->fp_next = mram->used_fp_list;
    mram->used_fp_list = node;
    node->owner = caller->krnl->mm;
  }

  free(list);
  pthread_mutex_unlock(&mram->lock);
  return 0;
}

#ifdef MM64
static addr_t *kernel_alloc_table(void)
{
  return calloc(PAGING64_LEVEL_ENTRIES, sizeof(addr_t));
}

static addr_t *kernel_walk_pte(struct krnl_t *krnl, addr_t addr, int create)
{
  addr_t pgd = 0, p4d = 0, pud = 0, pmd = 0, pt = 0;
  if (krnl == NULL)
    return NULL;

  if (get_pd_from_address(addr, &pgd, &p4d, &pud, &pmd, &pt) != 0)
    return NULL;

  if (krnl->krnl_pgd == NULL)
  {
    if (!create)
      return NULL;
    if (init_kernel_page_table(krnl) != 0)
      return NULL;
  }

  addr_t *p4d_ptr = (addr_t *)krnl->krnl_pgd[pgd];
  if (p4d_ptr == NULL && create) {
    p4d_ptr = kernel_alloc_table();
    if (p4d_ptr == NULL)
      return NULL;
    krnl->krnl_pgd[pgd] = (addr_t)p4d_ptr;
  }
  if (p4d_ptr == NULL)
    return NULL;

  addr_t *pud_ptr = (addr_t *)p4d_ptr[p4d];
  if (pud_ptr == NULL && create) {
    pud_ptr = kernel_alloc_table();
    if (pud_ptr == NULL)
      return NULL;
    p4d_ptr[p4d] = (addr_t)pud_ptr;
  }
  if (pud_ptr == NULL)
    return NULL;

  addr_t *pmd_ptr = (addr_t *)pud_ptr[pud];
  if (pmd_ptr == NULL && create) {
    pmd_ptr = kernel_alloc_table();
    if (pmd_ptr == NULL)
      return NULL;
    pud_ptr[pud] = (addr_t)pmd_ptr;
  }
  if (pmd_ptr == NULL)
    return NULL;

  addr_t *pt_ptr = (addr_t *)pmd_ptr[pmd];
  if (pt_ptr == NULL && create) {
    pt_ptr = kernel_alloc_table();
    if (pt_ptr == NULL)
      return NULL;
    pmd_ptr[pmd] = (addr_t)pt_ptr;
  }
  if (pt_ptr == NULL)
    return NULL;

  krnl->krnl_p4d = p4d_ptr;
  krnl->krnl_pud = pud_ptr;
  krnl->krnl_pmd = pmd_ptr;
  krnl->krnl_pt = pt_ptr;

  return &pt_ptr[pt];
}

static addr_t *kernel_get_pte(struct krnl_t *krnl, addr_t addr)
{
  return kernel_walk_pte(krnl, addr, 1);
}
#else
static addr_t *kernel_walk_pte(struct krnl_t *krnl, addr_t addr, int create)
{
  // Get page table entry for 32-bit paging
  // Direct access to kernel page directory

  (void)create;
  if (krnl == NULL || krnl->krnl_pgd == NULL)
    return NULL;

  return (addr_t *)&krnl->krnl_pgd[PAGING_PGN(addr)];
}

static addr_t *kernel_get_pte(struct krnl_t *krnl, addr_t addr)
{
  return kernel_walk_pte(krnl, addr, 1);
}
#endif

static addr_t kernel_vaddr_from_fpn(addr_t fpn)
{
  return KMEM_VADDR_BASE + fpn * KMEM_PAGE_SIZE;
}

static addr_t kernel_fpn_from_vaddr(addr_t addr)
{
  return (addr - KMEM_VADDR_BASE) / KMEM_PAGE_SIZE;
}

static int kernel_map_page(struct krnl_t *krnl, addr_t addr, addr_t fpn)
{
  // Map a virtual address to a physical frame in kernel page table
  // Initializes the page table entry for the mapping

  addr_t *pte = kernel_get_pte(krnl, addr);
  if (pte == NULL)
    return -1;

  *pte = 0;
#ifdef MM64
  SETBIT(*pte, PAGING64_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING64_PTE_SWAPPED_MASK);
  CLRBIT(*pte, PAGING64_PTE_DIRTY_MASK);
  PAGING64_PTE_SET_FPN(*pte, fpn);
#else
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
  CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);
  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
#endif

  return 0;
}

static void kernel_release_contiguous_region(struct pcb_t *caller, addr_t base_addr, int pgnum)
{
  // Clear kernel page mappings and return the backing frames to the free list

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mram == NULL || pgnum <= 0)
    return;

  for (int page = 0; page < pgnum; page++)
  {
    addr_t vaddr = base_addr + page * KMEM_PAGE_SIZE;
    addr_t *pte = kernel_walk_pte(caller->krnl, vaddr, 0);
    if (pte != NULL)
      *pte = 0;
  }

  addr_t first_fpn = kernel_fpn_from_vaddr(base_addr);
  struct memphy_struct *mram = caller->krnl->mram;

  pthread_mutex_lock(&mram->lock);
  for (int page = 0; page < pgnum; page++)
  {
    struct framephy_struct *node = kernel_get_used_frame_node(mram, first_fpn + page);
    if (node == NULL)
      continue;

    node->owner = NULL;
    node->fp_next = mram->free_fp_list;
    mram->free_fp_list = node;
  }
  pthread_mutex_unlock(&mram->lock);
}

static int kernel_alloc_contiguous_region(struct pcb_t *caller, addr_t size, addr_t *base_addr, addr_t *alloc_size)
{
  // Allocate contiguous physical frames and map them into kernel virtual space

  if (caller == NULL || caller->krnl == NULL || size == 0 || base_addr == NULL)
    return -1;

  addr_t aligned_size = KMEM_PAGE_ALIGN(size);
  int req_pgnum = aligned_size / KMEM_PAGE_SIZE;
  addr_t first_fpn = 0;

  if (kernel_allocate_contiguous_frames(caller, req_pgnum, &first_fpn) != 0)
    return -1;

  addr_t start_addr = kernel_vaddr_from_fpn(first_fpn);
  for (int page = 0; page < req_pgnum; page++)
  {
    addr_t vaddr = start_addr + page * KMEM_PAGE_SIZE;
    if (kernel_map_page(caller->krnl, vaddr, first_fpn + page) != 0)
    {
      kernel_release_contiguous_region(caller, start_addr, req_pgnum);
      return -1;
    }
  }

  *base_addr = start_addr;
  if (alloc_size != NULL)
    *alloc_size = aligned_size;

  return 0;
}

static int kernel_translate_vaddr(struct krnl_t *krnl, addr_t addr, addr_t *phys_addr)
{
  // Translate kernel virtual address to physical address
  // Returns the physical address corresponding to the virtual address

  addr_t *pte = kernel_walk_pte(krnl, addr, 0);
  addr_t offset;

  if (pte == NULL || phys_addr == NULL)
    return -1;

#ifdef MM64
  offset = addr & PAGING64_ADDR_OFFST_MASK;
#else
  offset = PAGING_OFFST(addr);
#endif

#ifdef MM64
  if (!PAGING64_PAGE_PRESENT(*pte))
    return -1;

  addr_t fpn = PAGING64_PTE_FPN(*pte);
#else
  if (!PAGING_PAGE_PRESENT(*pte))
    return -1;

  addr_t fpn = PAGING_FPN(*pte);
#endif
  *phys_addr = fpn * KMEM_PAGE_SIZE + offset;

  return 0;
}

static int kernel_region_valid(struct pcb_t *caller, int rgid, addr_t size, addr_t offset, struct vm_rg_struct **out_rg)
{
  // Validate that a kernel memory region is accessible for the given size and offset
  // Checks bounds and returns the region structure if valid

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  struct vm_rg_struct *rg = get_symrg_byid(caller->krnl->mm, rgid);
  if (rg == NULL || rg->rg_end <= rg->rg_start)
    return -1;

  addr_t region_size = rg->rg_end - rg->rg_start;
  if (size > region_size || offset > region_size - size)
    return -1;

  if (out_rg)
    *out_rg = rg;

  return 0;
}

int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  // Allocate kernel memory using contiguous physical frames
  // Wrapper function for __kmalloc with simplified interface

  if (caller == NULL || caller->krnl == NULL || size == 0)
    return -1;

  if (reg_index >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  addr_t alloc_addr = 0;
  addr_t ret = __kmalloc(caller, -1, reg_index, size, &alloc_addr);
  if (ret != (addr_t)-1)
  {
    caller->regs[reg_index] = alloc_addr;
#ifdef IODUMP
    printf("libkmem_malloc:532 pid=%u pc=%u alloc reg=%u size=%u addr=0x%llx\n",
           caller->pid, caller->pc, reg_index, size,
           (unsigned long long)alloc_addr);
    trace_dump_state(caller, "KMALLOC", size, reg_index, 0, 0);
#endif
  }
  return (ret == (addr_t)-1) ? -1 : 0;
}

addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  // Allocate contiguous kernel memory of specified size
  // Returns allocated address or -1 on failure
  // Parameters:
  //   caller: PCB of the calling process
  //   vmaid: VM area ID (unused for kernel alloc)
  //   rgid: Region index in symbol table
  //   size: Size of memory to allocate
  //   alloc_addr: Output parameter for allocated address

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL || size == 0 || alloc_addr == NULL)
    return (addr_t)-1;

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return (addr_t)-1;

  struct vm_rg_struct *symrg = &caller->krnl->mm->symrgtbl[rgid];
  if (symrg->rg_start != 0 || symrg->rg_end != 0)
    return (addr_t)-1;

  pthread_mutex_lock(&mmvm_lock);
  addr_t base_addr = 0;
  addr_t alloc_size = 0;
  if (kernel_alloc_contiguous_region(caller, size, &base_addr, &alloc_size) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return (addr_t)-1;
  }

  symrg->rg_start = base_addr;
  symrg->rg_end = base_addr + size;
  symrg->vmaid = 0;

  *alloc_addr = base_addr;
  pthread_mutex_unlock(&mmvm_lock);
  return base_addr;
}

static struct kmem_cache_slab_struct *create_cache_slab(addr_t base_addr, int slot_size, int slot_count)
{
  // Create a new slab structure for the cache pool
  // Initializes all slots as free and builds the free list

  struct kmem_cache_slab_struct *slab = malloc(sizeof(*slab));
  if (slab == NULL)
    return NULL;

  slab->start = base_addr;
  slab->slot_size = slot_size;
  slab->slot_count = slot_count;
  slab->free_count = slot_count;
  slab->free_list = NULL;
  slab->next = NULL;

  struct kmem_cache_slot_struct **tail = &slab->free_list;
  for (int idx = 0; idx < slot_count; idx++)
  {
    struct kmem_cache_slot_struct *slot = malloc(sizeof(*slot));
    if (slot == NULL)
    {
      while (slab->free_list)
      {
        struct kmem_cache_slot_struct *next = slab->free_list->next;
        free(slab->free_list);
        slab->free_list = next;
      }
      free(slab);
      return NULL;
    }
    slot->addr = base_addr + idx * slot_size;
    slot->next = NULL;
    *tail = slot;
    tail = &slot->next;
  }

  return slab;
}

static void cache_list_remove_slab(struct kmem_cache_slab_struct **head, struct kmem_cache_slab_struct *slab)
{
  // Remove a specific slab from the linked list
  // Used when moving slabs between empty/partial/full lists

  struct kmem_cache_slab_struct *prev = NULL;
  struct kmem_cache_slab_struct *curr = *head;

  while (curr != NULL)
  {
    if (curr == slab)
    {
      if (prev != NULL)
        prev->next = curr->next;
      else
        *head = curr->next;
      curr->next = NULL;
      return;
    }
    prev = curr;
    curr = curr->next;
  }
}

static void cache_list_add_slab(struct kmem_cache_slab_struct **head, struct kmem_cache_slab_struct *slab)
{
  // Add a slab to the front of the linked list
  slab->next = *head;
  *head = slab;
}

int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
  // Create a new kernel memory cache pool for fixed-size allocations
  // Allocates memory and initializes slab structures for efficient allocation

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;

  if (cache_pool_id >= PAGING_MAX_SYMTBL_SZ || size == 0 || align == 0)
    return -1;

  size_t usable_size = (size / align) * align;
  if (usable_size == 0)
    return -1;

  int slot_count = usable_size / align;
  if (slot_count == 0)
    return -1;

  pthread_mutex_lock(&mmvm_lock);

  struct mm_struct *mm = caller->krnl->mm;
  if (mm->kcpooltbl == NULL)
  {
    mm->kcpooltbl = calloc(cache_pool_id + 1, sizeof(*mm->kcpooltbl));
    if (mm->kcpooltbl == NULL)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
    mm->kcpooltbl_size = cache_pool_id + 1;
  }
  else if ((int)cache_pool_id >= mm->kcpooltbl_size)
  {
    int new_size = cache_pool_id + 1;
    struct kcache_pool_struct *new_table = calloc(new_size, sizeof(*new_table));
    if (new_table == NULL)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
    memcpy(new_table, mm->kcpooltbl, mm->kcpooltbl_size * sizeof(*new_table));
    free(mm->kcpooltbl);
    mm->kcpooltbl = new_table;
    mm->kcpooltbl_size = new_size;
  }

  struct kcache_pool_struct *pool = &mm->kcpooltbl[cache_pool_id];
  if (pool->size != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t alloc_addr = 0;
  addr_t alloc_size = 0;
  if (kernel_alloc_contiguous_region(caller, size, &alloc_addr, &alloc_size) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  pool->size = usable_size;
  pool->align = align;
  pool->storage = alloc_addr;
  pool->alloc_size = alloc_size;
  pool->slot_count = slot_count;
  pool->empty = NULL;
  pool->partial = NULL;
  pool->full = NULL;

  struct kmem_cache_slab_struct *slab = create_cache_slab(alloc_addr, align, slot_count);
  if (slab == NULL)
  {
    kernel_release_contiguous_region(caller, alloc_addr, KMEM_PAGE_ALIGN(size) / KMEM_PAGE_SIZE);
    memset(pool, 0, sizeof(*pool));
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  cache_list_add_slab(&pool->empty, slab);
  pthread_mutex_unlock(&mmvm_lock);
#ifdef IODUMP
  printf("libkmem_cache_pool_create:602 pid=%u pc=%u create cache_pool_id=%u size=%u align=%u storage=0x%llx\n",
         caller->pid, caller->pc, cache_pool_id, size, align,
         (unsigned long long)alloc_addr);
  trace_dump_state(caller, "KMEM_CACHE_CREATE", size, align, cache_pool_id, 0);
#endif
  return 0;
}

int libkmem_cache_alloc(struct pcb_t *proc, uint32_t reg_index, uint32_t cache_pool_id)
{
  // Allocate a single object from the specified cache pool
  // Returns 0 on success, -1 on failure

  if (proc == NULL || proc->krnl == NULL || proc->krnl->mm == NULL)
    return -1;

  if (cache_pool_id >= PAGING_MAX_SYMTBL_SZ || reg_index >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  addr_t addr = 0;
  addr_t result = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr);
  if (result != (addr_t)-1)
  {
    proc->regs[reg_index] = addr;
#ifdef IODUMP
    printf("libkmem_cache_alloc:632 pid=%u pc=%u alloc cache_pool_id=%u reg=%u addr=0x0\n",
           proc->pid, proc->pc, cache_pool_id, reg_index);
    trace_dump_state(proc, "KMEM_CACHE_ALLOC", cache_pool_id, reg_index, 0, 0);
#endif
  }
  return (result == (addr_t)-1) ? -1 : 0;
}

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{
  // Allocate a slot from the specified cache pool using slab allocation
  // Returns allocated address or -1 on failure

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL || alloc_addr == NULL)
    return (addr_t)-1;

  if (cache_pool_id < 0 || cache_pool_id >= PAGING_MAX_SYMTBL_SZ || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return (addr_t)-1;

  pthread_mutex_lock(&mmvm_lock);
  struct mm_struct *mm = caller->krnl->mm;
  if (mm->kcpooltbl == NULL || cache_pool_id >= mm->kcpooltbl_size)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return (addr_t)-1;
  }

  struct kcache_pool_struct *pool = &mm->kcpooltbl[cache_pool_id];
  if (pool->size == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return (addr_t)-1;
  }

  struct vm_rg_struct *symrg = &mm->symrgtbl[rgid];
  if (symrg->rg_start != 0 || symrg->rg_end != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return (addr_t)-1;
  }

  struct kmem_cache_slab_struct *slab = pool->partial ? pool->partial : pool->empty;
  if (slab == NULL || slab->free_list == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return (addr_t)-1;
  }

  struct kmem_cache_slot_struct *slot = slab->free_list;
  slab->free_list = slot->next;
  slab->free_count--;

  if (slab->free_count == 0)
  {
    cache_list_remove_slab(&pool->empty, slab);
    cache_list_remove_slab(&pool->partial, slab);
    cache_list_add_slab(&pool->full, slab);
  }
  else if (slab == pool->empty)
  {
    cache_list_remove_slab(&pool->empty, slab);
    cache_list_add_slab(&pool->partial, slab);
  }

  symrg->rg_start = slot->addr;
  symrg->rg_end = slot->addr + pool->size;
  symrg->vmaid = 0;

  *alloc_addr = slot->addr;

  free(slot);
  pthread_mutex_unlock(&mmvm_lock);
  return *alloc_addr;
}

int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  // Securely copy data from user space to kernel space
  // Performs address validation and bounds checking

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;

  if (source >= PAGING_MAX_SYMTBL_SZ || destination >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  if (size == 0)
    return 0;

  pthread_mutex_lock(&mmvm_lock);
  for (uint32_t idx = 0; idx < size; idx++)
  {
    BYTE data;
    if (__read_user_mem(caller, -1, source, offset + idx, &data) != 0)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
    if (__write_kernel_mem(caller, -1, destination, offset + idx, data) != 0)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
  }
  pthread_mutex_unlock(&mmvm_lock);
#ifdef IODUMP
  printf("libkmem_copy_from_user: pid=%u copy_from_user src=%u dst=%u off=%u size=%u\n",
         caller->pid, source, destination, offset, size);
  trace_dump_state(caller, "COPY_FROM_USER", source, destination, offset, size);
#endif
  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  // Securely copy data from kernel space to user space
  // Performs address validation and bounds checking

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;

  if (source >= PAGING_MAX_SYMTBL_SZ || destination >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  if (size == 0)
    return 0;

  pthread_mutex_lock(&mmvm_lock);
  for (uint32_t idx = 0; idx < size; idx++)
  {
    BYTE data;
    if (__read_kernel_mem(caller, -1, source, offset + idx, &data) != 0)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
    if (__write_user_mem(caller, -1, destination, offset + idx, data) != 0)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
  }
  pthread_mutex_unlock(&mmvm_lock);
#ifdef IODUMP
  printf("libkmem_copy_to_user: pid=%u copy_from_to src=%u dst=%u off=%u size=%u\n",
         caller->pid, source, destination, offset, size);
  trace_dump_state(caller, "COPY_TO_USER", source, destination, offset, size);
#endif
  return 0;
}

int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  // Read a single byte from kernel memory space
  // Validates address and performs virtual-to-physical translation

  struct vm_rg_struct *rg;
  if (kernel_region_valid(caller, rgid, 1, offset, &rg) != 0)
    return -1;

  addr_t vaddr = rg->rg_start + offset;
  addr_t phys;
  if (kernel_translate_vaddr(caller->krnl, vaddr, &phys) != 0)
    return -1;

  return MEMPHY_read(caller->krnl->mram, phys, data);
}

int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  // Write a single byte to kernel memory space
  // Validates address and performs virtual-to-physical translation

  struct vm_rg_struct *rg;
  if (kernel_region_valid(caller, rgid, 1, offset, &rg) != 0)
    return -1;

  addr_t vaddr = rg->rg_start + offset;
  addr_t phys;
  if (kernel_translate_vaddr(caller->krnl, vaddr, &phys) != 0)
    return -1;

  return MEMPHY_write(caller->krnl->mram, phys, value);
}

int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  // Read a single byte from user memory space
  // Validates user address bounds and performs translation

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL || data == NULL)
    return -1;

  struct vm_rg_struct *rg = get_symrg_byid(caller->krnl->mm, rgid);
  if (rg == NULL || rg->rg_end <= rg->rg_start)
    return -1;

  if (offset >= rg->rg_end - rg->rg_start)
    return -1;

  addr_t access_addr = rg->rg_start + offset;
  if (access_addr < rg->rg_start
#ifdef MM64
      || !is_user_vaddr_range(rg->rg_start, rg->rg_end)
      || !is_user_vaddr_range(access_addr, access_addr + 1)
#endif
     )
    return -1;

  return pg_getval(caller->krnl->mm, access_addr, data, caller);
}

int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  // Write a single byte to user memory space
  // Validates user address bounds and performs translation

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;

  struct vm_rg_struct *rg = get_symrg_byid(caller->krnl->mm, rgid);
  if (rg == NULL || rg->rg_end <= rg->rg_start)
    return -1;

  if (offset >= rg->rg_end - rg->rg_start)
    return -1;

  addr_t access_addr = rg->rg_start + offset;
  if (access_addr < rg->rg_start
#ifdef MM64
      || !is_user_vaddr_range(rg->rg_start, rg->rg_end)
      || !is_user_vaddr_range(access_addr, access_addr + 1)
#endif
     )
    return -1;

  return pg_setval(caller->krnl->mm, access_addr, value, caller);
}

#ifdef MM64
static void free_kmem_cache_slab_list(struct kmem_cache_slab_struct *slab)
{
  while (slab != NULL)
  {
    struct kmem_cache_slab_struct *next_slab = slab->next;
    struct kmem_cache_slot_struct *slot = slab->free_list;

    while (slot != NULL)
    {
      struct kmem_cache_slot_struct *next_slot = slot->next;
      free(slot);
      slot = next_slot;
    }

    free(slab);
    slab = next_slab;
  }
}

void free_kmem_cache_metadata(struct mm_struct *mm)
{
  if (mm == NULL || mm->kcpooltbl == NULL)
    return;

  for (int idx = 0; idx < mm->kcpooltbl_size; idx++)
  {
    struct kcache_pool_struct *pool = &mm->kcpooltbl[idx];

    free_kmem_cache_slab_list(pool->empty);
    free_kmem_cache_slab_list(pool->partial);
    free_kmem_cache_slab_list(pool->full);
    memset(pool, 0, sizeof(*pool));
  }

  free(mm->kcpooltbl);
  mm->kcpooltbl = NULL;
  mm->kcpooltbl_size = 0;
}

static int is_kernel_vaddr_range(addr_t start, addr_t end)
{
  return start >= KMEM_VADDR_BASE && end > start;
}

static int is_user_vaddr_range(addr_t start, addr_t end)
{
  addr_t pgd = 0;
  addr_t p4d = 0;
  addr_t pud = 0;
  addr_t pmd = 0;
  addr_t pt = 0;

  if (end <= start)
    return 0;

  if (get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt) != 0)
    return 0;

  if (get_pd_from_address(end - 1, &pgd, &p4d, &pud, &pmd, &pt) != 0)
    return 0;

  return (((start >> PAGING64_ADDR_PGD_HIBIT) & 1ULL) == 0) &&
         ((((end - 1) >> PAGING64_ADDR_PGD_HIBIT) & 1ULL) == 0);
}

static void clear_symrg_range(struct mm_struct *mm, addr_t start, addr_t end)
{
  if (mm == NULL || !is_kernel_vaddr_range(start, end))
    return;

  for (int idx = 0; idx < PAGING_MAX_SYMTBL_SZ; idx++)
  {
    struct vm_rg_struct *rg = &mm->symrgtbl[idx];

    if (rg->rg_start >= start && rg->rg_end <= end)
    {
      rg->rg_start = 0;
      rg->rg_end = 0;
      rg->vmaid = 0;
    }
  }
}

static void release_kernel_cache_pools(struct pcb_t *caller)
{
  struct mm_struct *mm = NULL;

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return;

  mm = caller->krnl->mm;
  if (mm->kcpooltbl == NULL)
    return;

  for (int idx = 0; idx < mm->kcpooltbl_size; idx++)
  {
    struct kcache_pool_struct *pool = &mm->kcpooltbl[idx];
    addr_t release_size = pool->alloc_size;
    addr_t storage_end = 0;

    if (pool->size == 0 || pool->storage == 0)
      continue;

    if (release_size == 0)
      release_size = KMEM_PAGE_ALIGN(pool->size);

    if (release_size == 0)
      continue;

    storage_end = pool->storage + release_size;
    kernel_release_contiguous_region(caller, pool->storage, release_size / KMEM_PAGE_SIZE);
    clear_symrg_range(mm, pool->storage, storage_end);
  }

  free_kmem_cache_metadata(mm);
}

static void release_kernel_symrg_allocations(struct pcb_t *caller)
{
  struct mm_struct *mm = NULL;

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return;

  mm = caller->krnl->mm;

  for (int idx = 0; idx < PAGING_MAX_SYMTBL_SZ; idx++)
  {
    struct vm_rg_struct *rg = &mm->symrgtbl[idx];
    addr_t size = 0;
    int pgnum = 0;

    if (!is_kernel_vaddr_range(rg->rg_start, rg->rg_end))
      continue;

    size = rg->rg_end - rg->rg_start;
    pgnum = KMEM_PAGE_ALIGN(size) / KMEM_PAGE_SIZE;
    if (pgnum <= 0)
      continue;

    kernel_release_contiguous_region(caller, rg->rg_start, pgnum);
    rg->rg_start = 0;
    rg->rg_end = 0;
    rg->vmaid = 0;
  }
}
#endif


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
#ifdef MM64
/*
 * pcb_memph_collect_recursive - Traverse page tables to free RAM frames and Swap slots.
 */
static void pcb_memph_collect_recursive(struct pcb_t *caller, addr_t *table, int level)
{
  if (table == NULL)
    return;

  for (int i = 0; i < PAGING64_LEVEL_ENTRIES; i++)
  {
    addr_t pte = table[i];
    if (pte == 0)
      continue;

    if (level < 4) // PGD, P4D, PUD, PMD
    {
      pcb_memph_collect_recursive(caller, (addr_t *)pte, level + 1);
    }
    else // Leaf PT entry
    {
      if (PAGING64_PAGE_PRESENT(pte) && !PAGING64_PAGE_SWAPPED(pte))
      {
        addr_t fpn = PAGING64_PTE_FPN(pte);
        MEMPHY_put_freefp(caller->krnl->mram, fpn);
      }
      else if (PAGING64_PAGE_SWAPPED(pte))
      {
        addr_t swpfpn = PAGING64_PTE_SWP(pte);
        MEMPHY_put_freefp(caller->krnl->active_mswp, swpfpn);
      }
    }
  }
}
#endif

int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);

#ifdef MM64
  release_kernel_cache_pools(caller);
  release_kernel_symrg_allocations(caller);

  /* 1. Traverse hierarchical page tables to free all physical resources (RAM & Swap) */
  if (caller->krnl->mm->pgd != NULL)
    pcb_memph_collect_recursive(caller, caller->krnl->mm->pgd, 0);

  /* 2. Free the FIFO management nodes */
  struct pgn_t *pg = caller->krnl->mm->fifo_pgn;
  while (pg != NULL)
  {
    struct pgn_t *next = pg->pg_next;
    free(pg);
    pg = next;
  }
  caller->krnl->mm->fifo_pgn = NULL;
#else
  int pagenum;
  uint32_t pte;
  addr_t fpn;
  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->krnl->mm->pgd[pagenum];

    if (pte == 0)
      continue;

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else if ((pte & PAGING_PTE_SWAPPED_MASK) != 0)
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }
#endif

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* FIFO: evict the oldest page (tail of the list, since new pages are prepended) */
  if (!pg)
    return -1;

  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }

  *retpgn = pg->pgn;

  /* Detach the tail node */
  if (prev != NULL)
    prev->pg_next = NULL;
  else
    mm->fifo_pgn = NULL; /* list had only one element */

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

// #endif
