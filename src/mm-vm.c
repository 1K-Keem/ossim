/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  struct vm_area_struct *pvma = mm->mmap;

  if (mm->mmap == NULL)
    return NULL;

  int vmait = pvma->vm_id;

  while (vmait < vmaid)
  {
    if (pvma == NULL)
      return NULL;

    pvma = pvma->vm_next;
    vmait = pvma->vm_id;
  }

  return pvma;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t size, addr_t alignedsz)
{
  struct vm_rg_struct * newrg;
  /* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
  //struct vm_area_struct *cur_vma = get_vma_by_num(caller->kernl->mm, vmaid);

  //newrg = malloc(sizeof(struct vm_rg_struct));

  /* TODO: update the newrg boundary
  // newrg->rg_start = ...
  // newrg->rg_end = ...
  */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);

  newrg = malloc(sizeof(struct vm_rg_struct));
  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end = newrg->rg_start + size;
  /* END TODO */

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
{
  if (vmastart >= vmaend)
    return -1;

  struct vm_area_struct *cur_area = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_area == NULL)
    return -1;

  /* Check the planned range [vmastart, vmaend] against all other VMAs */
  struct vm_area_struct *vma = caller->krnl->mm->mmap;
  while (vma != NULL)
  {
    if (vma != cur_area && OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end))
      return -1; /* Overlap with another VMA detected */
    vma = vma->vm_next;
  }

  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
  if (newrg == NULL)
    return -1;

  /* Align the increment size to page boundary */
#ifdef MM64
  addr_t inc_amt = PAGING64_PAGE_ALIGNSZ(inc_sz);
  int incnumpage = (int)(inc_amt / PAGING64_PAGESZ);
#else
  addr_t inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);
  int incnumpage = (int)(inc_amt / PAGING_PAGESZ);
#endif

  /* Obtain current VMA and record its old end (sbrk) */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->krnl->mm, vmaid);
  if (cur_vma == NULL) {
    free(newrg);
    return -1;
  }

  addr_t old_end = cur_vma->sbrk;

  /* Compute planned new region boundary */
  addr_t new_end = old_end + inc_amt;

  /* Validate that the new region does not overlap any other VMA */
  if (validate_overlap_vm_area(caller, vmaid, old_end, new_end) < 0) {
    free(newrg);
    return -1; /* Overlap detected, allocation fails */
  }

  /* Expand the VMA limit and sbrk pointer */
  cur_vma->vm_end = new_end;
  cur_vma->sbrk   = new_end;

  /* Map the newly expanded region into physical RAM */
  if (vm_map_ram(caller, old_end, new_end, old_end, incnumpage, newrg) < 0) {
    /* Roll back if mapping fails */
    cur_vma->vm_end = old_end;
    cur_vma->sbrk   = old_end;
    free(newrg);
    return -1;
  }

  /* Enlist the newly mapped region into the VMA free region list
   * so future alloc requests can pick it up */
  enlist_vm_rg_node(&cur_vma->vm_freerg_list, newrg);

  return 0;
}

// #endif