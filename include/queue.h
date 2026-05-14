
#ifndef QUEUE_H
#define QUEUE_H

#include "common.h"

#define MAX_QUEUE_SIZE 50

struct queue_t {
	struct pcb_t * proc[MAX_QUEUE_SIZE];
	int size;
};

/*
 * Returns 0 when the process was queued, -1 when the queue is invalid or full.
 */
int enqueue(struct queue_t * q, struct pcb_t * proc);

struct pcb_t * dequeue(struct queue_t * q);

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc);

int empty(struct queue_t * q);

#endif
