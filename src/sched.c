#include "queue.h"
#define OSSIM_PROJECT_SCHED_H
#include "sched.h"
#undef OSSIM_PROJECT_SCHED_H
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

/* MAX_PRIO is defined in os-cfg.h (via common.h -> queue.h) */

static struct queue_t ready_queue;
static struct queue_t run_queue;
static struct queue_t running_list;
static pthread_mutex_t queue_lock;

#ifdef MLQ_SCHED
/*
 * Multi-Level Queue ready queues, one per priority level.
 * slot[i] tracks remaining dispatches for priority i in the current round.
 * According to MLQ policy: slot[prio] = MAX_PRIO - prio
 *   prio=0 (highest) gets MAX_PRIO   slots
 *   prio=1           gets MAX_PRIO-1 slots
 *   ...
 *   prio=MAX_PRIO-1  gets 1          slot
 */
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
#endif

/*
 * queue_empty - check whether all ready queues are empty.
 * Returns 1 (true) if empty, 0 (false) if at least one process is waiting.
 */
int queue_empty(void)
{
#ifdef MLQ_SCHED
	int prio;
	for (prio = 0; prio < MAX_PRIO; prio++)
		if (!empty(&mlq_ready_queue[prio]))
			return 0;
	return 1;
#else
	return (empty(&ready_queue) && empty(&run_queue));
#endif
}

void init_scheduler(void)
{

	ready_queue.size = 0;
	run_queue.size = 0;
	running_list.size = 0;

#ifdef MLQ_SCHED
	int i;
	for (i = 0; i < MAX_PRIO; i++)
	{
		mlq_ready_queue[i].size = 0;
		slot[i] = MAX_PRIO - i; /* prio 0 → 3 slots, prio 1 → 2, prio 2 → 1 */
	}
#endif

	pthread_mutex_init(&queue_lock, NULL);
}

void finish_scheduler(void)
{
	pthread_mutex_destroy(&queue_lock);
}

#ifdef MLQ_SCHED
/*
 * get_mlq_proc - MLQ dispatcher.
 *
 * Policy (stateful, per the assignment spec):
 *   Each priority level prio is allocated slot[prio] = MAX_PRIO - prio
 *   dispatches per round.  We scan from highest priority (prio=0) to lowest
 *   and dispatch from the first queue that is both non-empty AND has slots
 *   remaining.  When all slots across every level are exhausted we reset the
 *   slot counters and begin a new round.
 */
struct pcb_t *get_mlq_proc(void)
{
	struct pcb_t *proc = NULL;
	int prio;

	pthread_mutex_lock(&queue_lock);

	/* --- Pass 1: try to dispatch from the highest-priority non-empty
	 *             queue that still has slot budget. --- */
	for (prio = 0; prio < MAX_PRIO; prio++)
	{
		if (!empty(&mlq_ready_queue[prio]) && slot[prio] > 0)
		{
			proc = dequeue(&mlq_ready_queue[prio]);
			slot[prio]--;
			break;
		}
	}

	/* --- Pass 2: if every slot is exhausted (end of round), reset
	 *             and retry so we never starve a waiting process. --- */
	if (proc == NULL)
	{
		for (prio = 0; prio < MAX_PRIO; prio++)
			slot[prio] = MAX_PRIO - prio;

		for (prio = 0; prio < MAX_PRIO; prio++)
		{
			if (!empty(&mlq_ready_queue[prio]) && slot[prio] > 0)
			{
				proc = dequeue(&mlq_ready_queue[prio]);
				slot[prio]--;
				break;
			}
		}
	}

	/* --- End-of-round housekeeping: if all slots just hit zero after
	 *     this dispatch, reset now so the next get_mlq_proc() starts
	 *     a fresh round immediately rather than going through Pass 2. --- */
	{
		int all_zero = 1;
		for (prio = 0; prio < MAX_PRIO; prio++)
		{
			if (slot[prio] > 0)
			{
				all_zero = 0;
				break;
			}
		}
		if (all_zero)
		{
			for (prio = 0; prio < MAX_PRIO; prio++)
				slot[prio] = MAX_PRIO - prio;
		}
	}

	pthread_mutex_unlock(&queue_lock);
	return proc;
}

void put_mlq_proc(struct pcb_t *proc)
{
	/* Return a preempted/timeslice-expired process to its priority queue. */
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t *proc)
{
	/* Enqueue a newly loaded process into its priority queue. */
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	/* Also track in running_list so find_proc() can locate this process */
	enqueue(&running_list, proc);
	pthread_mutex_unlock(&queue_lock);
}

struct pcb_t *get_proc(void) { return get_mlq_proc(); }
void put_proc(struct pcb_t *proc) { put_mlq_proc(proc); }
void add_proc(struct pcb_t *proc) { add_mlq_proc(proc); }

/*
 * find_proc - find a process by PID.
 * All processes are tracked in running_list from the moment they are loaded
 * (via add_mlq_proc) until they finish (via finish_proc).
 */
struct pcb_t *find_proc(uint32_t pid)
{
	pthread_mutex_lock(&queue_lock);
	int i;
	for (i = 0; i < running_list.size; i++) {
		if (running_list.proc[i] != NULL &&
		    running_list.proc[i]->pid == pid) {
			struct pcb_t *p = running_list.proc[i];
			pthread_mutex_unlock(&queue_lock);
			return p;
		}
	}
	pthread_mutex_unlock(&queue_lock);
	return NULL;
}

/*
 * finish_proc - remove a finished process from running_list.
 */
void finish_proc(struct pcb_t *proc)
{
	pthread_mutex_lock(&queue_lock);
	int i;
	for (i = 0; i < running_list.size; i++) {
		if (running_list.proc[i] == proc) {
			int j;
			for (j = i; j < running_list.size - 1; j++)
				running_list.proc[j] = running_list.proc[j + 1];
			running_list.proc[running_list.size - 1] = NULL;
			running_list.size--;
			break;
		}
	}
	pthread_mutex_unlock(&queue_lock);
}

#else /* !MLQ_SCHED — simple FIFO ready queue */

struct pcb_t *get_proc(void)
{
	struct pcb_t *proc;

	pthread_mutex_lock(&queue_lock);
	proc = dequeue(&ready_queue);
	pthread_mutex_unlock(&queue_lock);

	return proc;
}

void put_proc(struct pcb_t *proc)
{
	/* Return a timeslice-expired process to the back of the ready queue. */
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t *proc)
{
	/* Add a newly loaded process to the back of the ready queue. */
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

#endif /* MLQ_SCHED */

/*
 * find_proc - search all queues for a process with the given PID.
 * (non-MLQ fallback — also used when MLQ is off)
 */
#ifndef MLQ_SCHED
struct pcb_t *find_proc(uint32_t pid)
{
	pthread_mutex_lock(&queue_lock);
	int i;
	for (i = 0; i < running_list.size; i++) {
		if (running_list.proc[i] != NULL &&
		    running_list.proc[i]->pid == pid) {
			struct pcb_t *p = running_list.proc[i];
			pthread_mutex_unlock(&queue_lock);
			return p;
		}
	}
	for (i = 0; i < ready_queue.size; i++) {
		if (ready_queue.proc[i] != NULL &&
		    ready_queue.proc[i]->pid == pid) {
			struct pcb_t *p = ready_queue.proc[i];
			pthread_mutex_unlock(&queue_lock);
			return p;
		}
	}
	pthread_mutex_unlock(&queue_lock);
	return NULL;
}

void finish_proc(struct pcb_t *proc)
{
	pthread_mutex_lock(&queue_lock);
	int i;
	for (i = 0; i < running_list.size; i++) {
		if (running_list.proc[i] == proc) {
			int j;
			for (j = i; j < running_list.size - 1; j++)
				running_list.proc[j] = running_list.proc[j + 1];
			running_list.proc[running_list.size - 1] = NULL;
			running_list.size--;
			break;
		}
	}
	pthread_mutex_unlock(&queue_lock);
}
#endif /* !MLQ_SCHED */