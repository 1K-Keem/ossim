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
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
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
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  if (cur_vma == NULL) {
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
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
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
  printf("liballoc:%d\n", __LINE__);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
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
  printf("libfree:%d\n", __LINE__);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
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

  uint32_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, bring it into RAM via swap */
    addr_t vicpgn, swpfpn;
    addr_t vicfpn;
    addr_t tgtfpn;

    /* Find victim page using FIFO policy */
    if (find_victim_page(caller->krnl->mm, &vicpgn) == -1)
      return -1;

    /* Get the physical frame number of the victim page */
    uint32_t vicpte = pte_get_entry(caller, vicpgn);
    vicfpn = PAGING_FPN(vicpte);

    /* Get a free frame in MEMSWP to store the evicted victim */
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
      return -1;

    /* The target frame for the requested page reuses the victim's RAM frame */
    tgtfpn = vicfpn;

    /* Remember the swap offset of the requested page (if it was previously swapped out) */
    addr_t req_swpoff = PAGING_SWP(pte);
    int req_was_swapped = GETVAL(pte, PAGING_PTE_SWAPPED_MASK, 30);

    /* Step 1: Evict victim page — copy victim frame from RAM to swap */
    struct sc_regs regs;
    regs.a1 = SYSMEM_SWP_OP;
    regs.a2 = vicfpn;   /* source: victim frame in MEMRAM */
    regs.a3 = swpfpn;   /* dest:   free slot in MEMSWP */
    _syscall(caller->krnl, caller->pid, 17, &regs);

    /* Step 2: If requested page was previously swapped out, load it back from swap */
    if (req_was_swapped)
    {
      __swap_cp_page(caller->krnl->active_mswp, req_swpoff,
                     caller->krnl->mram, tgtfpn);
      /* Return the swap slot of the requested page to the free pool */
      MEMPHY_put_freefp(caller->krnl->active_mswp, req_swpoff);
    }

    /* Step 3: Mark victim page as swapped out in its PTE */
    pte_set_swap(caller, vicpgn, 0, swpfpn);

    /* Step 4: Mark requested page as present in RAM at tgtfpn */
    pte_set_fpn(caller, pgn, tgtfpn);

    /* Track the newly loaded page in the FIFO replacement list */
    enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(caller, pgn));

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
//int off = PAGING_OFFST(addr);
  addr_t fpn;

#ifdef MM64
  pgn = addr >> PAGING64_ADDR_PT_SHIFT;
#else
  pgn = PAGING_PGN(addr);
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
//int off = PAGING_OFFST(addr);
  addr_t fpn;

#ifdef MM64
  pgn = addr >> PAGING64_ADDR_PT_SHIFT;
#else
  pgn = PAGING_PGN(addr);
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

  pg_getval(caller->krnl->mm, currg->rg_start + offset, data, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  if (val == -1)
    return -1;

  *destination = data;
#ifdef IODUMP
  printf("libread:%d\n", __LINE__);
  /* Note: libread does not dump page table per design */
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

  pg_setval(caller->krnl->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
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
  printf("libwrite:%d\n", __LINE__);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
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

static int kernel_allocate_contiguous_frames(struct pcb_t *caller, int req_pgnum, addr_t *start_fpn)
{
  // Allocate a contiguous block of physical frames for kernel memory
  // Finds consecutive free frames and marks them as used

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
  return calloc(PAGING64_MAX_PGN, sizeof(addr_t));
}

static addr_t *kernel_get_pte(struct krnl_t *krnl, addr_t addr)
{
  addr_t pgd = 0, p4d = 0, pud = 0, pmd = 0, pt = 0;
  if (get_pd_from_address(addr, &pgd, &p4d, &pud, &pmd, &pt) != 0)
    return NULL;

  if (krnl->krnl_pgd == NULL && init_kernel_page_table(krnl) != 0)
    return NULL;

  addr_t *p4d_ptr = (addr_t *)krnl->krnl_pgd[pgd];
  if (p4d_ptr == NULL) {
    p4d_ptr = kernel_alloc_table();
    if (p4d_ptr == NULL)
      return NULL;
    krnl->krnl_pgd[pgd] = (addr_t)p4d_ptr;
  }

  addr_t *pud_ptr = (addr_t *)p4d_ptr[p4d];
  if (pud_ptr == NULL) {
    pud_ptr = kernel_alloc_table();
    if (pud_ptr == NULL)
      return NULL;
    p4d_ptr[p4d] = (addr_t)pud_ptr;
  }

  addr_t *pmd_ptr = (addr_t *)pud_ptr[pud];
  if (pmd_ptr == NULL) {
    pmd_ptr = kernel_alloc_table();
    if (pmd_ptr == NULL)
      return NULL;
    pud_ptr[pud] = (addr_t)pmd_ptr;
  }

  addr_t *pt_ptr = (addr_t *)pmd_ptr[pmd];
  if (pt_ptr == NULL) {
    pt_ptr = kernel_alloc_table();
    if (pt_ptr == NULL)
      return NULL;
    pmd_ptr[pmd] = (addr_t)pt_ptr;
  }

  krnl->krnl_p4d = p4d_ptr;
  krnl->krnl_pud = pud_ptr;
  krnl->krnl_pmd = pmd_ptr;
  krnl->krnl_pt = pt_ptr;

  return &pt_ptr[pt];
}
#else
static addr_t *kernel_get_pte(struct krnl_t *krnl, addr_t addr)
{
  // Get page table entry for 32-bit paging
  // Direct access to kernel page directory

  return (addr_t *)&krnl->krnl_pgd[PAGING_PGN(addr)];
}
#endif

static int kernel_map_page(struct krnl_t *krnl, addr_t addr, addr_t fpn)
{
  // Map a virtual address to a physical frame in kernel page table
  // Initializes the page table entry for the mapping

  addr_t *pte = kernel_get_pte(krnl, addr);
  if (pte == NULL)
    return -1;

  init_pte(pte, 1, fpn, 0, 0, 0, 0);
  return 0;
}

static int kernel_translate_vaddr(struct krnl_t *krnl, addr_t addr, addr_t *phys_addr)
{
  // Translate kernel virtual address to physical address
  // Returns the physical address corresponding to the virtual address

  addr_t *pte = kernel_get_pte(krnl, addr);
  addr_t offset;

  if (pte == NULL || phys_addr == NULL)
    return -1;

#ifdef MM64
  offset = addr & PAGING64_ADDR_OFFST_MASK;
#else
  offset = PAGING_OFFST(addr);
#endif

  if (!PAGING_PAGE_PRESENT(*pte))
    return -1;

  addr_t fpn = PAGING_FPN(*pte);
  *phys_addr = fpn * (
#ifdef MM64
      PAGING64_PAGESZ
#else
      PAGING_PAGESZ
#endif
  ) + offset;

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
  if (rg == NULL || rg->rg_start == 0 || rg->rg_end <= rg->rg_start)
    return -1;

  if (offset + size > rg->rg_end - rg->rg_start)
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

#ifdef MM64
  addr_t alloc_size = PAGING64_PAGE_ALIGNSZ(size);
  int req_pgnum = alloc_size / PAGING64_PAGESZ;
#else
  addr_t alloc_size = PAGING_PAGE_ALIGNSZ(size);
  int req_pgnum = alloc_size / PAGING_PAGESZ;
#endif

  addr_t first_fpn = 0;

  pthread_mutex_lock(&mmvm_lock);
  if (kernel_allocate_contiguous_frames(caller, req_pgnum, &first_fpn) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return (addr_t)-1;
  }

  addr_t base_addr = first_fpn * (
#ifdef MM64
      PAGING64_PAGESZ
#else
      PAGING_PAGESZ
#endif
  );

  for (int page = 0; page < req_pgnum; page++)
  {
    addr_t vaddr = base_addr + page * (
#ifdef MM64
        PAGING64_PAGESZ
#else
        PAGING_PAGESZ
#endif
    );
    if (kernel_map_page(caller->krnl, vaddr, first_fpn + page) != 0)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return (addr_t)-1;
    }
  }

  symrg->rg_start = base_addr;
  symrg->rg_end = base_addr + alloc_size;
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
    slot->next = slab->free_list;
    slab->free_list = slot;
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

  addr_t alloc_addr = 0;
  if (__kmalloc(caller, -1, cache_pool_id, size, &alloc_addr) == (addr_t)-1)
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

  pool->size = usable_size;
  pool->align = align;
  pool->storage = alloc_addr;
  pool->slot_count = slot_count;
  pool->empty = NULL;
  pool->partial = NULL;
  pool->full = NULL;

  struct kmem_cache_slab_struct *slab = create_cache_slab(alloc_addr, align, slot_count);
  if (slab == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  cache_list_add_slab(&pool->empty, slab);
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
  // Allocate a single object from the specified cache pool
  // Returns 0 on success, -1 on failure

  if (proc == NULL || proc->krnl == NULL || proc->krnl->mm == NULL)
    return -1;

  if (cache_pool_id >= PAGING_MAX_SYMTBL_SZ || reg_index >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  addr_t addr = 0;
  addr_t result = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr);
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

  struct vm_rg_struct *symrg = &mm->symrgtbl[rgid];
  symrg->rg_start = slot->addr;
  symrg->rg_end = slot->addr + pool->align;
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
  if (rg == NULL || rg->rg_start == 0 || rg->rg_end <= rg->rg_start)
    return -1;

  if (offset >= rg->rg_end - rg->rg_start)
    return -1;

  return pg_getval(caller->krnl->mm, rg->rg_start + offset, data, caller);
}

int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  // Write a single byte to user memory space
  // Validates user address bounds and performs translation

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;

  struct vm_rg_struct *rg = get_symrg_byid(caller->krnl->mm, rgid);
  if (rg == NULL || rg->rg_start == 0 || rg->rg_end <= rg->rg_start)
    return -1;

  if (offset >= rg->rg_end - rg->rg_start)
    return -1;

  return pg_setval(caller->krnl->mm, rg->rg_start + offset, value, caller);
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  addr_t fpn;
  uint32_t pte;

#ifdef MM64
  struct pgn_t *pg = caller->krnl->mm->fifo_pgn;
  while (pg != NULL)
  {
    pte = pte_get_entry(caller, pg->pgn);

    if (pte == 0)
    {
      pg = pg->pg_next;
      continue;
    }

    if (PAGING_PAGE_PRESENT(pte) && ((pte & PAGING_PTE_SWAPPED_MASK) == 0))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else if ((pte & PAGING_PTE_SWAPPED_MASK) != 0)
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }

    pg = pg->pg_next;
  }
#else
  int pagenum;
  for (pagenum = 0; pagenum < PAGING_MAX_PGN; pagenum++)
  {
    pte = caller->krnl->mm->pgd[pagenum];

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
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