/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "queue.h"
#include "sched.h"
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>

static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

static struct queue_t running_list;
#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
static int curr_prio;
static int curr_slot;
#endif

static struct pcb_t *find_proc_locked(struct queue_t *queue, uint32_t pid)
{
	int i;

	if (queue == NULL)
		return NULL;

	for (i = 0; i < queue->size; i++)
		if (queue->proc[i] != NULL && queue->proc[i]->pid == pid)
			return queue->proc[i];

	return NULL;
}

int queue_empty(void)
{
	int is_empty;

	pthread_mutex_lock(&queue_lock);
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++) {
		if (!empty(&mlq_ready_queue[prio])) {
			pthread_mutex_unlock(&queue_lock);
			return -1;
		}
	}
#endif
	is_empty = empty(&ready_queue) && empty(&run_queue);
	pthread_mutex_unlock(&queue_lock);

	return is_empty;
}

void init_scheduler(void)
{
#ifdef MLQ_SCHED
	int i;

	for (i = 0; i < MAX_PRIO; i++) {
		mlq_ready_queue[i].size = 0;
		slot[i] = MAX_PRIO - i;
	}
	curr_prio = 0;
	curr_slot = slot[0];
#endif
	ready_queue.size = 0;
	run_queue.size = 0;
	running_list.size = 0;
	pthread_mutex_init(&queue_lock, NULL);
}

struct pcb_t *find_proc(uint32_t pid)
{
	struct pcb_t *proc = NULL;

	pthread_mutex_lock(&queue_lock);
	proc = find_proc_locked(&running_list, pid);
#ifdef MLQ_SCHED
	if (proc == NULL) {
		int prio;

		for (prio = 0; prio < MAX_PRIO && proc == NULL; prio++)
			proc = find_proc_locked(&mlq_ready_queue[prio], pid);
	}
#else
	if (proc == NULL)
		proc = find_proc_locked(&ready_queue, pid);
	if (proc == NULL)
		proc = find_proc_locked(&run_queue, pid);
#endif
	pthread_mutex_unlock(&queue_lock);

	return proc;
}

void finish_proc(struct pcb_t *proc)
{
	if (proc == NULL)
		return;

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc);
	pthread_mutex_unlock(&queue_lock);
}

#ifdef MLQ_SCHED
/*
 * MLQ uses more slots for higher-priority queues. All ready/running queue
 * mutations are protected by queue_lock to avoid duplicate dispatches across
 * CPUs and stale entries during requeue/finish.
 */
struct pcb_t *get_mlq_proc(void)
{
	struct pcb_t *proc = NULL;
	int attempts;

	pthread_mutex_lock(&queue_lock);
	for (attempts = 0; attempts < MAX_PRIO; attempts++) {
		if (curr_slot > 0 && !empty(&mlq_ready_queue[curr_prio])) {
			proc = dequeue(&mlq_ready_queue[curr_prio]);
			curr_slot--;
			break;
		}

		curr_prio = (curr_prio + 1) % MAX_PRIO;
		curr_slot = slot[curr_prio];
	}

	if (proc != NULL)
		enqueue(&running_list, proc);
	pthread_mutex_unlock(&queue_lock);

	return proc;
}

void put_mlq_proc(struct pcb_t *proc)
{
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t *proc)
{
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->mlq_ready_queue = mlq_ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

struct pcb_t *get_proc(void)
{
	return get_mlq_proc();
}

void put_proc(struct pcb_t *proc)
{
	return put_mlq_proc(proc);
}

void add_proc(struct pcb_t *proc)
{
	return add_mlq_proc(proc);
}
#else
struct pcb_t *get_proc(void)
{
	struct pcb_t *proc = NULL;

	pthread_mutex_lock(&queue_lock);
	proc = dequeue(&ready_queue);
	if (proc == NULL)
		proc = dequeue(&run_queue);
	if (proc != NULL)
		enqueue(&running_list, proc);
	pthread_mutex_unlock(&queue_lock);

	return proc;
}

void put_proc(struct pcb_t *proc)
{
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list, proc);
	enqueue(&run_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t *proc)
{
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}
#endif
