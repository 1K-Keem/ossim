/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include "queue.h"
#define OSSIM_PROJECT_SCHED_H
#include "sched.h"
#undef OSSIM_PROJECT_SCHED_H
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

#if defined(MM64)

/*
 * alloc_page_table - Allocate one zero-filled 64-bit page-table level.
 */
static addr_t *alloc_page_table(void)
{
  return calloc(PAGING64_LEVEL_ENTRIES, sizeof(addr_t));
}

/*
 * is_kernel_address - Identify whether a canonical address belongs to kernel space.
 */
static int is_kernel_address(addr_t addr)
{
  return ((addr >> PAGING64_ADDR_PGD_HIBIT) & 1ULL) != 0;
}

/*
 * validate_page_range - Validate an aligned mapping range before creating entries.
 */
static int validate_page_range(addr_t addr, int pgnum)
{
  addr_t last_addr = 0;
  addr_t span = 0;
  addr_t pgd = 0;
  addr_t p4d = 0;
  addr_t pud = 0;
  addr_t pmd = 0;
  addr_t pt = 0;

  if ((addr & PAGING64_ADDR_OFFST_MASK) != 0 || pgnum < 0)
    return -1;

  if (pgnum == 0)
    return 0;

  span = ((addr_t)pgnum - 1) * PAGING64_PAGESZ;
  last_addr = addr + span;

  if (last_addr < addr)
    return -1;

  if (get_pd_from_address(addr, &pgd, &p4d, &pud, &pmd, &pt) != 0)
    return -1;

  if (get_pd_from_address(last_addr, &pgd, &p4d, &pud, &pmd, &pt) != 0)
    return -1;

  if (is_kernel_address(addr) != is_kernel_address(last_addr))
    return -1;

  return 0;
}

static void free_mm_tables_recursive(addr_t *table, int level);
static void release_frame_list(struct pcb_t *caller, struct framephy_struct *head);
static void free_frame_list_nodes(struct framephy_struct *head);

/*
 * init_kernel_page_table - Initialize the shared kernel-space root page directory.
 */
int init_kernel_page_table(struct krnl_t *krnl)
{
  if (krnl == NULL)
    return -1;

  if (krnl->krnl_pgd == NULL)
    krnl->krnl_pgd = alloc_page_table();

  if (krnl->krnl_pgd == NULL)
    return -1;

  krnl->krnl_p4d = NULL;
  krnl->krnl_pud = NULL;
  krnl->krnl_pmd = NULL;
  krnl->krnl_pt = NULL;

  return 0;
}

/*
 * walk_pte - Traverse the 5-level page-table tree and optionally allocate missing levels.
 */
static addr_t *walk_pte(struct pcb_t *caller, addr_t addr, int create)
{
  struct krnl_t *krnl = NULL;
  struct mm_struct *mm = NULL;
  addr_t *pgd_ptr = NULL;
  addr_t *p4d_ptr = NULL;
  addr_t *pud_ptr = NULL;
  addr_t *pmd_ptr = NULL;
  addr_t *pt_ptr = NULL;
  addr_t pgd = 0;
  addr_t p4d = 0;
  addr_t pud = 0;
  addr_t pmd = 0;
  addr_t pt = 0;
  int kernel_addr = 0;

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return NULL;

  if (get_pd_from_address(addr, &pgd, &p4d, &pud, &pmd, &pt) != 0)
    return NULL;

  krnl = caller->krnl;
  mm = krnl->mm;
  kernel_addr = is_kernel_address(addr);

  if (kernel_addr) {
    if (krnl->krnl_pgd == NULL && create) {
      if (init_kernel_page_table(krnl) != 0)
        return NULL;
    }
    pgd_ptr = krnl->krnl_pgd;
  } else {
    if (mm->pgd == NULL && create)
      mm->pgd = alloc_page_table();
    pgd_ptr = mm->pgd;
  }

  if (pgd_ptr == NULL)
    return NULL;

  p4d_ptr = (addr_t *)pgd_ptr[pgd];
  if (p4d_ptr == NULL && create) {
    p4d_ptr = alloc_page_table();
    if (p4d_ptr == NULL)
      return NULL;
    pgd_ptr[pgd] = (addr_t)p4d_ptr;
  }
  if (p4d_ptr == NULL)
    return NULL;
  if (kernel_addr) {
    krnl->krnl_p4d = p4d_ptr;
  } else {
    mm->p4d = p4d_ptr;
  }

  pud_ptr = (addr_t *)p4d_ptr[p4d];
  if (pud_ptr == NULL && create) {
    pud_ptr = alloc_page_table();
    if (pud_ptr == NULL)
      return NULL;
    p4d_ptr[p4d] = (addr_t)pud_ptr;
  }
  if (pud_ptr == NULL)
    return NULL;
  if (kernel_addr) {
    krnl->krnl_pud = pud_ptr;
  } else {
    mm->pud = pud_ptr;
  }

  pmd_ptr = (addr_t *)pud_ptr[pud];
  if (pmd_ptr == NULL && create) {
    pmd_ptr = alloc_page_table();
    if (pmd_ptr == NULL)
      return NULL;
    pud_ptr[pud] = (addr_t)pmd_ptr;
  }
  if (pmd_ptr == NULL)
    return NULL;
  if (kernel_addr) {
    krnl->krnl_pmd = pmd_ptr;
  } else {
    mm->pmd = pmd_ptr;
  }

  pt_ptr = (addr_t *)pmd_ptr[pmd];
  if (pt_ptr == NULL && create) {
    pt_ptr = alloc_page_table();
    if (pt_ptr == NULL)
      return NULL;
    pmd_ptr[pmd] = (addr_t)pt_ptr;
  }
  if (pt_ptr == NULL)
    return NULL;
  if (kernel_addr) {
    krnl->krnl_pt = pt_ptr;
  } else {
    mm->pt = pt_ptr;
  }

  return &pt_ptr[pt];
}

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING64_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING64_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING64_PTE_DIRTY_MASK);

      PAGING64_PTE_SET_FPN(*pte, fpn);
    }
    else
    { // page swapped
      CLRBIT(*pte, PAGING64_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING64_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING64_PTE_DIRTY_MASK);

      PAGING64_PTE_SET_SWPTYP(*pte, swptyp);
      PAGING64_PTE_SET_SWP(*pte, swpoff);
    }
  }

  return 0;
}


/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Extract page direactories */
	*pgd = (addr&PAGING64_ADDR_PGD_MASK)>>PAGING64_ADDR_PGD_LOBIT;
	*p4d = (addr&PAGING64_ADDR_P4D_MASK)>>PAGING64_ADDR_P4D_LOBIT;
	*pud = (addr&PAGING64_ADDR_PUD_MASK)>>PAGING64_ADDR_PUD_LOBIT;
	*pmd = (addr&PAGING64_ADDR_PMD_MASK)>>PAGING64_ADDR_PMD_LOBIT;
	*pt = (addr&PAGING64_ADDR_PT_MASK)>>PAGING64_ADDR_PT_LOBIT;

	/* Enforce canonical addressing rules */
	addr_t top_bits = addr >> 57;
	addr_t bit56 = (addr >> 56) & 1;
	if (bit56 == 0) {
		if (top_bits != 0) return -1; // user space
	} else {
		if (top_bits != 0x7F) return -1; // kernel space (7 bits of 1)
	}

	return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table 
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Shift the address to get page num and perform the mapping*/
	if (pgn > (~(addr_t)0 >> PAGING64_ADDR_PT_SHIFT))
		return -1;

	return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                         pgd,p4d,pud,pmd,pt);
}


/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  addr_t *pte = NULL;
	
#ifdef MM64	
  /* Create the paging path on demand and write a swapped PTE. */
  pte = walk_pte(caller, pgn << PAGING64_ADDR_PT_SHIFT, 1);
  if (pte == NULL)
    return -1;

  *pte = 0;
  addr_t pte_val = 0;
  CLRBIT(pte_val, PAGING64_PTE_PRESENT_MASK);
  SETBIT(pte_val, PAGING64_PTE_SWAPPED_MASK);

  PAGING64_PTE_SET_SWPTYP(pte_val, swptyp);
  PAGING64_PTE_SET_SWP(pte_val, swpoff);

  *pte = pte_val;
#else
  struct krnl_t *krnl = caller->krnl;
  uint32_t pte_val = krnl->mm->pgd[pgn];
  CLRBIT(pte_val, PAGING_PTE_PRESENT_MASK);
  SETBIT(pte_val, PAGING_PTE_SWAPPED_MASK);

  SETVAL(pte_val, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(pte_val, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
  krnl->mm->pgd[pgn] = pte_val;
#endif

  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  addr_t *pte = NULL;
	
#ifdef MM64	
  /* Create the paging path on demand and write a resident-frame PTE. */
  pte = walk_pte(caller, pgn << PAGING64_ADDR_PT_SHIFT, 1);
  if (pte == NULL)
    return -1;

  *pte = 0;
  addr_t pte_val = 0;
  SETBIT(pte_val, PAGING64_PTE_PRESENT_MASK);
  CLRBIT(pte_val, PAGING64_PTE_SWAPPED_MASK);

  PAGING64_PTE_SET_FPN(pte_val, fpn);

  *pte = pte_val;
#else
  struct krnl_t *krnl = caller->krnl;
  uint32_t pte_val = krnl->mm->pgd[pgn];
  SETBIT(pte_val, PAGING_PTE_PRESENT_MASK);
  CLRBIT(pte_val, PAGING_PTE_SWAPPED_MASK);

  SETVAL(pte_val, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
  krnl->mm->pgd[pgn] = pte_val;
#endif

  return 0;
}


/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
addr_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  addr_t pte = 0;
	
  /* Perform multi-level page mapping */
#ifdef MM64
  /* Read an existing PTE without allocating missing page-table levels. */
  addr_t *pte_ptr = walk_pte(caller, pgn << PAGING64_ADDR_PT_SHIFT, 0);
  if (pte_ptr != NULL)
    pte = *pte_ptr;
#else
  struct krnl_t *krnl = caller->krnl;
  pte = krnl->mm->pgd[pgn];
#endif
	
  return pte;
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct pcb_t *caller, addr_t pgn, addr_t pte_val)
{
#ifdef MM64
    /* Store a raw PTE value after lazily creating the paging path. */
    addr_t *pte = walk_pte(caller, pgn << PAGING64_ADDR_PT_SHIFT, 1);
    if (pte == NULL)
      return -1;
    *pte = pte_val;
#else
	struct krnl_t *krnl = caller->krnl;
	krnl->mm->pgd[pgn]=pte_val;
#endif

	return 0;
}


/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum)                      // num of mapping page
{
  int pgit = 0;
  addr_t pattern = PAGING64_DUMMY_PTE;
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;

  /* Reject non-canonical or cross-space ranges before mapping any page. */
  if (validate_page_range(addr, pgnum) != 0)
    return -1;

  /* Populate dummy PTEs only; no physical frame is allocated. */
  for (pgit = 0; pgit < pgnum; pgit++) {
    if (pte_set_entry(caller, pgn + pgit, pattern) != 0)
      return -1;
  }

  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum,                      // num of mapping page
                    struct framephy_struct *frames, // list of the mapped frames
                    struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                   // no guarantee all given pages are mapped
  struct framephy_struct *fpit = frames;
  int pgit = 0;
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL ||
      ret_rg == NULL || validate_page_range(addr, pgnum) != 0) {
    release_frame_list(caller, frames);
    return (addr_t)-1;
  }

  /* update the rg_end and rg_start of ret_rg */
  ret_rg->rg_start = addr;
  ret_rg->rg_end = addr + pgnum * PAGING64_PAGESZ;

  for (pgit = 0; pgit < pgnum; pgit++) {
    if (walk_pte(caller, (pgn + pgit) << PAGING64_ADDR_PT_SHIFT, 1) == NULL) {
      ret_rg->rg_end = addr;
      release_frame_list(caller, frames);
      return (addr_t)-1;
    }
  }

  /* map range of frame to address space */
  for (pgit = 0; pgit < pgnum && fpit != NULL; pgit++) {
    struct framephy_struct *next = fpit->fp_next;

    if (pte_set_fpn(caller, pgn + pgit, fpit->fpn) != 0) {
      fpit->fp_next = next;
      release_frame_list(caller, fpit);
      ret_rg->rg_end = addr + pgit * PAGING64_PAGESZ;
      return (addr_t)-1;
    }

    /* Enqueue new usage page */
    if (enlist_pgn_node(&caller->krnl->mm->fifo_pgn, pgn + pgit) != 0) {
      pte_set_entry(caller, pgn + pgit, 0);
      MEMPHY_put_freefp(caller->krnl->mram, fpit->fpn);
      free(fpit);
      release_frame_list(caller, next);
      ret_rg->rg_end = addr + pgit * PAGING64_PAGESZ;
      return (addr_t)-1;
    }

    free(fpit);
    fpit = next;
  }

  free_frame_list_nodes(fpit);

  /* update real mapped region */
  ret_rg->rg_end = addr + pgit * PAGING64_PAGESZ;

  return 0;
}

static void release_frame_list(struct pcb_t *caller, struct framephy_struct *head)
{
  struct framephy_struct *curr = head;

  while (curr != NULL) {
    struct framephy_struct *next = curr->fp_next;
    if (caller != NULL && caller->krnl != NULL && caller->krnl->mram != NULL)
      MEMPHY_put_freefp(caller->krnl->mram, curr->fpn);
    free(curr);
    curr = next;
  }
}

static void free_frame_list_nodes(struct framephy_struct *head)
{
  struct framephy_struct *curr = head;

  while (curr != NULL) {
    struct framephy_struct *next = curr->fp_next;
    free(curr);
    curr = next;
  }
}

static struct framephy_struct *new_frame_node(addr_t fpn)
{
  struct framephy_struct *node = malloc(sizeof(struct framephy_struct));

  if (node == NULL)
    return NULL;

  node->fpn = fpn;
  node->fp_next = NULL;
  node->owner = NULL;

  return node;
}

static int find_present_victim_in_proc(struct pcb_t *proc, addr_t *retpgn, addr_t *retpte)
{
  addr_t vicpgn = 0;
  addr_t vicpte = 0;

  if (proc == NULL || proc->krnl == NULL || proc->krnl->mm == NULL ||
      retpgn == NULL || retpte == NULL)
    return -1;

  while (find_victim_page(proc->krnl->mm, &vicpgn) == 0) {
    vicpte = pte_get_entry(proc, vicpgn);
    if (PAGING64_PAGE_PRESENT(vicpte) && !PAGING64_PAGE_SWAPPED(vicpte)) {
      *retpgn = vicpgn;
      *retpte = vicpte;
      return 0;
    }
  }

  return -1;
}

static struct pcb_t *find_global_victim(struct pcb_t *caller, addr_t *retpgn, addr_t *retpte)
{
  struct queue_t *running_list = NULL;
  int idx = 0;

  if (find_present_victim_in_proc(caller, retpgn, retpte) == 0)
    return caller;

  if (caller == NULL || caller->krnl == NULL)
    return NULL;

  running_list = caller->krnl->running_list;
  if (running_list == NULL)
    return NULL;

  for (idx = 0; idx < running_list->size; idx++) {
    struct pcb_t *proc = running_list->proc[idx];

    if (proc == NULL || proc == caller)
      continue;

    if (find_present_victim_in_proc(proc, retpgn, retpte) == 0)
      return proc;
  }

  return NULL;
}

int swap_out_victim_page(struct pcb_t *caller, addr_t *retfpn)
{
  struct pcb_t *victim_proc = NULL;
  addr_t vicpgn = 0;
  addr_t swpfpn = 0;
  addr_t vicpte = 0;
  int sched_locked = 0;

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL ||
      caller->krnl->mram == NULL || caller->krnl->active_mswp == NULL ||
      retfpn == NULL)
    return -1;

  if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) != 0)
    return -1;

  scheduler_lock();
  sched_locked = 1;

  victim_proc = find_global_victim(caller, &vicpgn, &vicpte);
  if (victim_proc == NULL) {
    scheduler_unlock();
    sched_locked = 0;
    MEMPHY_put_freefp(caller->krnl->active_mswp, swpfpn);
    return -1;
  }

  *retfpn = PAGING64_PTE_FPN(vicpte);
  if (__swap_cp_page(caller->krnl->mram, *retfpn, caller->krnl->active_mswp, swpfpn) != 0) {
    scheduler_unlock();
    sched_locked = 0;
    MEMPHY_put_freefp(caller->krnl->active_mswp, swpfpn);
    return -1;
  }

  if (pte_set_swap(victim_proc, vicpgn, caller->krnl->active_mswp_id, swpfpn) != 0) {
    scheduler_unlock();
    sched_locked = 0;
    MEMPHY_put_freefp(caller->krnl->active_mswp, swpfpn);
    return -1;
  }

  if (sched_locked)
    scheduler_unlock();

  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  addr_t fpn;
  int pgit;
  struct framephy_struct *newfp_str = NULL;
  struct framephy_struct *head = NULL;
  struct framephy_struct *tail = NULL;

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mram == NULL ||
      req_pgnum < 0 || frm_lst == NULL)
    return -1;

  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) != 0 &&
        swap_out_victim_page(caller, &fpn) != 0)
    {
      release_frame_list(caller, head);
      return -3000;
    }

    newfp_str = new_frame_node(fpn);
    if (newfp_str == NULL) {
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
      release_frame_list(caller, head);
      return -1;
    }

    if (head == NULL) {
      head = newfp_str;
      tail = newfp_str;
    } else {
      tail->fp_next = newfp_str;
      tail = newfp_str;
    }
  }

  *frm_lst = head;

  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;
  int pgnum = incpgnum;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  ret_alloc = alloc_pages_range(caller, pgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  /* Out of memory */
  if (ret_alloc == -3000)
  {
    return -1;
  }

  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
   if (vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg) == (addr_t)-1)
     return -1;

  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;

    BYTE data;
    /* Bug 3.3.3: propagate MEMPHY errors instead of silently continuing */
    if (MEMPHY_read(mpsrc, addrsrc, &data) != 0)
      return -1;
    if (MEMPHY_write(mpdst, addrdst, data) != 0)
      return -1;
  }

  return 0;
}

/*
 *Initialize a empty Memory Management instance
 * @mm:     self mm
 * @caller: mm owner
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
  int symid;

  /* init page table directory */
  mm->pgd = alloc_page_table();
  if (mm->pgd == NULL || vma0 == NULL)
    return -1;

  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt = NULL;

  /* Ensure every process context points to the shared kernel page-table root. */
  if (caller != NULL && caller->krnl != NULL && init_kernel_page_table(caller->krnl) != 0)
    return -1;

  mm->fifo_pgn = NULL;
  mm->kcpooltbl = NULL;
  mm->kcpooltbl_size = 0;
  for (symid = 0; symid < PAGING_MAX_SYMTBL_SZ; symid++) {
    mm->symrgtbl[symid].vmaid = 0;
    mm->symrgtbl[symid].rg_start = 0;
    mm->symrgtbl[symid].rg_end = 0;
    mm->symrgtbl[symid].rg_next = NULL;
  }


  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  vma0->vm_freerg_list = NULL;
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /* update VMA0 next */
  vma0->vm_next = NULL;

  /* Point vma owner backward */
  vma0->vm_mm = mm; 

  /* update mmap */
  mm->mmap = vma0;

  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  struct vm_rg_struct *curr = *rglist;
  struct vm_rg_struct *prev = NULL;

  if (rgnode->rg_start >= rgnode->rg_end)
    return -1;

  while (curr != NULL && curr->rg_start < rgnode->rg_start) {
    prev = curr;
    curr = curr->rg_next;
  }

  rgnode->rg_next = curr;
  if (prev == NULL) {
    *rglist = rgnode;
  } else {
    prev->rg_next = rgnode;
  }

  if (curr != NULL && rgnode->rg_end == curr->rg_start) {
    rgnode->rg_end = curr->rg_end;
    rgnode->rg_next = curr->rg_next;
    free(curr);
  }

  if (prev != NULL && prev->rg_end == rgnode->rg_start) {
    prev->rg_end = rgnode->rg_end;
    prev->rg_next = rgnode->rg_next;
    free(rgnode);
  }

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  if (pnode == NULL)
    return -1;

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("\n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
//addr_t pgn_start;//, pgn_end;
//addr_t pgit;
//struct krnl_t *krnl = caller->krnl;

  struct mm_struct *mm = NULL;
  addr_t pgd=0;
  addr_t p4d=0;
  addr_t pud=0;
  addr_t pmd=0;
  addr_t pt=0;
  addr_t pgn_start = 0;
  addr_t pgn_end = 0;
  addr_t pgn = 0;

  if (caller == NULL || caller->krnl == NULL || caller->krnl->mm == NULL)
    return -1;

  mm = caller->krnl->mm;

  printf("print_pgtbl:\n");

  pgn_start = start >> PAGING64_ADDR_PT_SHIFT;
  if (end == (addr_t)-1)
    pgn_end = PAGING64_LEVEL_ENTRIES - 1;
  else
    pgn_end = end >> PAGING64_ADDR_PT_SHIFT;

  if (pgn_end < pgn_start)
    return 0;

  /* Traverse the page map and dump the page directory entries */
  for (pgn = pgn_start; pgn <= pgn_end; pgn++)
  {
    addr_t *pte_ptr = NULL;
    addr_t addr = pgn << PAGING64_ADDR_PT_SHIFT;
    addr_t *root = NULL;
    addr_t *p4d_ptr = NULL;
    addr_t *pud_ptr = NULL;
    addr_t *pmd_ptr = NULL;

    if (get_pd_from_pagenum(pgn, &pgd, &p4d, &pud, &pmd, &pt) != 0)
      continue;

    root = is_kernel_address(addr) ? caller->krnl->krnl_pgd : mm->pgd;
    if (root == NULL || root[pgd] == 0)
      continue;

    p4d_ptr = (addr_t *)root[pgd];
    if (p4d_ptr[p4d] == 0)
      continue;

    pud_ptr = (addr_t *)p4d_ptr[p4d];
    if (pud_ptr[pud] == 0)
      continue;

    pmd_ptr = (addr_t *)pud_ptr[pud];
    if (pmd_ptr[pmd] == 0)
      continue;

    pte_ptr = walk_pte(caller, addr, 0);
    if (pte_ptr == NULL || *pte_ptr == 0)
      continue;

    addr_t pte = *pte_ptr;
    printf("vmap: VPN=%016lx", (unsigned long)pgn);
    if (PAGING64_PAGE_PRESENT(pte)) {
      printf(" -> PFN=%016lx (Present)\n", (unsigned long)PAGING64_PTE_FPN(pte));
    } else if (PAGING64_PAGE_SWAPPED(pte)) {
      printf(" -> SWAP=%016lx (Swapped)\n", (unsigned long)PAGING64_PTE_SWP(pte));
    } else {
      printf(" -> Invalid PTE\n");
    }
  }

  return 0;
}

/*
 * free_mm_tables_recursive - Traverse and free all tables in the 5-level tree.
 */
static void free_mm_tables_recursive(addr_t *table, int level)
{
  if (table == NULL)
    return;

  if (level < 4) // PGD, P4D, PUD, PMD have pointers to next levels
  {
    for (int i = 0; i < PAGING64_LEVEL_ENTRIES; i++)
    {
      if (table[i] != 0)
      {
        free_mm_tables_recursive((addr_t *)table[i], level + 1);
      }
    }
  }
  free(table);
}

void free_kernel_page_table(struct krnl_t *krnl)
{
  if (krnl == NULL)
    return;

  free_mm_tables_recursive(krnl->krnl_pgd, 0);
  krnl->krnl_pgd = NULL;
  krnl->krnl_p4d = NULL;
  krnl->krnl_pud = NULL;
  krnl->krnl_pmd = NULL;
  krnl->krnl_pt = NULL;
}

/*
 * free_mm - Comprehensive cleanup of the mm_struct and its children.
 */
void free_mm(struct mm_struct *mm)
{
  if (mm == NULL)
    return;

  /* 1. Free all page table levels */
  free_mm_tables_recursive(mm->pgd, 0);

  /* 2. Free VMAs and their associated region lists */
  extern void free_vma_list(struct vm_area_struct * vma);
  free_vma_list(mm->mmap);

  /* 3. Free kernel cache metadata and symrgtbl region nodes */
  extern void free_rg_list(struct vm_rg_struct * rg);
  extern void free_kmem_cache_metadata(struct mm_struct * mm);
  free_kmem_cache_metadata(mm);
  for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++)
  {
    free_rg_list(mm->symrgtbl[i].rg_next);
  }

  /* 4. Free the mm_struct itself */
  free(mm);
}

#endif  //def MM64
