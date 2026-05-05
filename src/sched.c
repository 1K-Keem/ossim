#include "queue.h"
#include "sched.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_PRIO 3

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
	pthread_mutex_unlock(&queue_lock);
}

struct pcb_t *get_proc(void) { return get_mlq_proc(); }
void put_proc(struct pcb_t *proc) { put_mlq_proc(proc); }
void add_proc(struct pcb_t *proc) { add_mlq_proc(proc); }

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