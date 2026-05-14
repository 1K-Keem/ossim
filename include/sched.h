#if defined(_PTHREAD_H) && !defined(OSSIM_PROJECT_SCHED_H)
#include_next <sched.h>
#else

#ifndef OSSIM_SCHED_H
#define OSSIM_SCHED_H

#include "common.h"

#ifndef MLQ_SCHED
#define MLQ_SCHED
#endif

#ifndef MAX_PRIO
#define MAX_PRIO 140
#endif

int queue_empty(void);

void init_scheduler(void);
void finish_scheduler(void);
void scheduler_lock(void);
void scheduler_unlock(void);
struct pcb_t * find_proc(uint32_t pid);
void finish_proc(struct pcb_t * proc);

/* Get the next process from ready queue */
struct pcb_t * get_proc(void);

/* Put a process back to run queue */
void put_proc(struct pcb_t * proc);

/* Add a new process to ready queue */
void add_proc(struct pcb_t * proc);

#endif /* OSSIM_SCHED_H */
#endif /* pthread system sched.h detour */
