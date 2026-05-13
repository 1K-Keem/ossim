#if defined(_PTHREAD_H) && !defined(_BITS_PTHREADTYPES_COMMON_H)
  /* Being pulled in from within pthread.h before pthreadtypes.h:
   * forward to the real system sched.h and do NOT set QUEUE_H. */
  #ifndef _PROJECT_SCHED_DETOUR_H
  #define _PROJECT_SCHED_DETOUR_H
  #include_next <sched.h>
  #endif
#else

#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"

#ifndef MLQ_SCHED
#define MLQ_SCHED
#endif

#define MAX_PRIO 140

int queue_empty(void);

void init_scheduler(void);
void finish_scheduler(void);
struct pcb_t * find_proc(uint32_t pid);
void finish_proc(struct pcb_t * proc);

/* Get the next process from ready queue */
struct pcb_t * get_proc(void);

/* Put a process back to run queue */
void put_proc(struct pcb_t * proc);

/* Add a new process to ready queue */
void add_proc(struct pcb_t * proc);

#endif /* QUEUE_H */
#endif /* pthread detour */