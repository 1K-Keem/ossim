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
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */

#include "mm.h"
#ifdef MM64
#include "mm64.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  MEMPHY_mv_csr - move MEMPHY cursor
 *  @mp: memphy struct
 *  @offset: offset
 */
int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
   int numstep = 0;

   mp->cursor = 0;
   while (numstep < offset && numstep < mp->maxsz)
   {
      /* Traverse sequentially */
      mp->cursor = (mp->cursor + 1) % mp->maxsz;
      numstep++;
   }

   return 0;
}

/*
 *  MEMPHY_seq_read - read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL)
      return -1;

   if (!mp->rdmflg)
      return -1; /* Not compatible mode for sequential read */

   MEMPHY_mv_csr(mp, addr);
   *value = (BYTE)mp->storage[addr];

   return 0;
}

/*
 *  MEMPHY_read read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL)
      return -1;

   pthread_mutex_lock(&mp->lock);
   if (addr >= (addr_t)mp->maxsz) {
      pthread_mutex_unlock(&mp->lock);
      return -1;
   }

   if (mp->rdmflg)
      *value = mp->storage[addr];
   else /* Sequential access device */
   {
      int ret = MEMPHY_seq_read(mp, addr, value);
      pthread_mutex_unlock(&mp->lock);
      return ret;
   }

   pthread_mutex_unlock(&mp->lock);
   return 0;
}

/*
 *  MEMPHY_seq_write - write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{

   if (mp == NULL)
      return -1;

   if (!mp->rdmflg)
      return -1; /* Not compatible mode for sequential read */

   MEMPHY_mv_csr(mp, addr);
   mp->storage[addr] = value;

   return 0;
}

/*
 *  MEMPHY_write-write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
   if (mp == NULL)
      return -1;

   pthread_mutex_lock(&mp->lock);
   if (addr >= (addr_t)mp->maxsz) {
      pthread_mutex_unlock(&mp->lock);
      return -1;
   }

   if (mp->rdmflg)
      mp->storage[addr] = data;
   else /* Sequential access device */
   {
      int ret = MEMPHY_seq_write(mp, addr, data);
      pthread_mutex_unlock(&mp->lock);
      return ret;
   }

   pthread_mutex_unlock(&mp->lock);
   return 0;
}

/*
 *  MEMPHY_format-format MEMPHY device
 *  @mp: memphy struct
 */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
   /* This setting come with fixed constant PAGESZ */
   int numfp = mp->maxsz / pagesz;
   struct framephy_struct *newfst, *fst;
   int iter = 0;

   if (numfp <= 0)
      return -1;

   /* Init head of free framephy list */
   fst = malloc(sizeof(struct framephy_struct));
   fst->fpn = iter;
   fst->fp_next = NULL;
   mp->free_fp_list = fst;

   /* We have list with first element, fill in the rest num-1 element member*/
   for (iter = 1; iter < numfp; iter++)
   {
      newfst = malloc(sizeof(struct framephy_struct));
      newfst->fpn = iter;
      newfst->fp_next = NULL;
      fst->fp_next = newfst;
      fst = newfst;
   }

   return 0;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
   struct framephy_struct *fp;

   if (mp == NULL || retfpn == NULL)
      return -1;

   pthread_mutex_lock(&mp->lock);
   fp = mp->free_fp_list;
   if (fp == NULL) {
      pthread_mutex_unlock(&mp->lock);
      return -1;
   }

   *retfpn = fp->fpn;
   mp->free_fp_list = fp->fp_next;
   pthread_mutex_unlock(&mp->lock);

   /* MEMPHY is iteratively used up until its exhausted
    * No garbage collector acting then it not been released
    */
   free(fp);

   return 0;
}

int MEMPHY_dump(struct memphy_struct *mp)
{
  /* Dump memphy content mp->storage for tracing the memory content */
  if (mp == NULL || mp->storage == NULL)
    return -1;

  printf("MEMPHY Dump (size: %d bytes):\n", mp->maxsz);
  printf("Address  | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F | ASCII\n");
  printf("---------|---------------------------------------------------|------\n");

  for (addr_t addr = 0; addr < (addr_t)mp->maxsz; addr += 16) {
    printf("%08llX | ", (unsigned long long)addr);

    // Print hex values
    for (int i = 0; i < 16; i++) {
      if (addr + i < (addr_t)mp->maxsz) {
        printf("%02X ", mp->storage[addr + i]);
      } else {
        printf("   ");
      }
    }

    printf("| ");

    // Print ASCII representation
    for (int i = 0; i < 16; i++) {
      if (addr + i < (addr_t)mp->maxsz) {
        BYTE b = mp->storage[addr + i];
        if (b >= 32 && b <= 126) {
          printf("%c", b);
        } else {
          printf(".");
        }
      } else {
        printf(" ");
      }
    }

    printf("\n");
  }

  printf("\n");
  return 0;
}

int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
   struct framephy_struct *fp;
   struct framephy_struct *newnode;

   if (mp == NULL)
      return -1;

   newnode = malloc(sizeof(struct framephy_struct));
   if (newnode == NULL)
      return -1;

   /* Create new node with value fpn */
   pthread_mutex_lock(&mp->lock);
   fp = mp->free_fp_list;
   newnode->fpn = fpn;
   newnode->fp_next = fp;
   mp->free_fp_list = newnode;
   pthread_mutex_unlock(&mp->lock);

   return 0;
}

/*
 *  Init MEMPHY struct
 */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
   pthread_mutex_init(&mp->lock, NULL);
   mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
   mp->maxsz = max_size;
   mp->free_fp_list = NULL;
   mp->used_fp_list = NULL;
   memset(mp->storage, 0, max_size * sizeof(BYTE));

#ifdef MM64
   MEMPHY_format(mp, PAGING64_PAGESZ);
#else
   MEMPHY_format(mp, PAGING_PAGESZ);
#endif

   mp->rdmflg = (randomflg != 0) ? 1 : 0;

   if (!mp->rdmflg) /* Not Ramdom acess device, then it serial device*/
      mp->cursor = 0;

   return 0;
}

// #endif
