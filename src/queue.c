#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        if (q == NULL || proc == NULL || q->size >= MAX_QUEUE_SIZE)
                return;

        q->proc[q->size++] = proc;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        struct pcb_t *proc;
        int i;

        if (empty(q))
                return NULL;

        proc = q->proc[0];
        for (i = 1; i < q->size; i++)
                q->proc[i - 1] = q->proc[i];

        q->size--;
        q->proc[q->size] = NULL;
        return proc;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        int i;

        if (empty(q) || proc == NULL)
                return NULL;

        for (i = 0; i < q->size; i++) {
                if (q->proc[i] == proc) {
                        struct pcb_t *removed = q->proc[i];
                        int j;

                        for (j = i + 1; j < q->size; j++)
                                q->proc[j - 1] = q->proc[j];

                        q->size--;
                        q->proc[q->size] = NULL;
                        return removed;
                }
        }

        return NULL;
}
