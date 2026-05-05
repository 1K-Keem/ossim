/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#ifndef OSMM_H
#define OSMM_H

#include <pthread.h>
#include <stdint.h>

#define MM_PAGING
#define PAGING_MAX_MMSWP 4 /* max number of supported swapped space */
#define PAGING_MAX_SYMTBL_SZ 30

/* 
 * @bksysnet: in long address mode of 64bit or original 32bit
 * the address type need to be redefined
 */

#ifdef MM64
#define ADDR_TYPE uint64_t
#else
#define ADDR_TYPE uint32_t
#endif

typedef char BYTE;
typedef ADDR_TYPE addr_t;
//typedef unsigned int uint32_t;

/* 
 * @bksysnet: the format string need to be redefined
 *            based on the address mode
 */
#ifdef MM64
#define FORMAT_ADDR "%lu"
#define FORMATX_ADDR "%16p"
#else
#define FORMAT_ADDR "%d"
#define FORMATX_ADDR "%08x"
#endif

struct pgn_t{
   addr_t pgn;
   struct pgn_t *pg_next; 
};

/*
 *  Memory region struct
 */
struct vm_rg_struct {
   int vmaid;

   addr_t rg_start;
   addr_t rg_end;

   struct vm_rg_struct *rg_next;
};

/*
 *  Memory area struct
 */
struct vm_area_struct {
   unsigned long vm_id;
   addr_t vm_start;
   addr_t vm_end;

   addr_t sbrk;
/*
 * Derived field
 * unsigned long vm_limit = vm_end - vm_start
 */
   struct mm_struct *vm_mm;
   struct vm_rg_struct *vm_freerg_list;
   struct vm_area_struct *vm_next;
};

struct kmem_cache_slot_struct {
   addr_t addr;  // Virtual address of the slot
   struct kmem_cache_slot_struct *next;  // Next free slot in the list
};

struct kmem_cache_slab_struct {
   addr_t start;  // Starting virtual address of the slab
   int slot_size;  // Size of each slot in bytes
   int slot_count;  // Total number of slots in the slab
   int free_count;  // Number of free slots remaining
   struct kmem_cache_slot_struct *free_list;  // Linked list of free slots
   struct kmem_cache_slab_struct *next;  // Next slab in the cache pool
};

/* 
 * Kernel cache pool struct
 * Manages multiple slabs for efficient memory allocation of fixed-size objects
 */
struct kcache_pool_struct {
   int size;  // Total size of the allocated memory block
   int align;  // Alignment size for each slot
   int slot_count;  // Total number of slots across all slabs
   addr_t storage;  // Base virtual address of the allocated memory
   struct kmem_cache_slab_struct *empty;  // Slabs with all slots free
   struct kmem_cache_slab_struct *partial;  // Slabs with some slots allocated
   struct kmem_cache_slab_struct *full;  // Slabs with all slots allocated
};


/* 
 * Memory management struct
 */
struct mm_struct {
#ifdef MM64
   addr_t *pgd;
   addr_t *p4d;
   addr_t *pud;
   addr_t *pmd;
   addr_t *pt;
#else
   uint32_t *pgd;
#endif

   struct vm_area_struct *mmap;

   /* Currently we support a fixed number of symbol */
   struct vm_rg_struct symrgtbl[PAGING_MAX_SYMTBL_SZ];

   /* list of free page */
   struct pgn_t *fifo_pgn;

   /* kmem cache pool */
   struct kcache_pool_struct *kcpooltbl;  // Array of kernel cache pools
   int kcpooltbl_size;  // Number of cache pools in the table

};


/*
 * FRAME/MEM PHY struct
 */
struct framephy_struct { 
   addr_t fpn;
   struct framephy_struct *fp_next;

   /* Resereed for tracking allocated framed */
   struct mm_struct* owner;
};

struct memphy_struct {
   /* Basic field of data and size */
   BYTE *storage;
   int maxsz;
   
   /* Sequential device fields */ 
   int rdmflg;
   int cursor;

   /* Management structure */
   struct framephy_struct *free_fp_list;
   struct framephy_struct *used_fp_list;
   pthread_mutex_t lock;
};

#endif
