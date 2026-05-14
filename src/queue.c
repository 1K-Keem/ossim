#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

int enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* Append process to the back of the queue (FIFO). */
        if (q == NULL || q->size >= MAX_QUEUE_SIZE)
                return -1;

        q->proc[q->size] = proc;
        q->size++;
        return 0;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /*
         * Remove and return the front element (FIFO order).
         * Within each priority level, processes run in the order they arrived.
         */
        if (q == NULL || q->size == 0)
                return NULL;

        struct pcb_t *proc = q->proc[0];

        /* Shift all remaining elements one position to the left. */
        for (int i = 0; i < q->size - 1; i++)
                q->proc[i] = q->proc[i + 1];

        q->size--;
        return proc;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* Remove a specific process from any position in the queue. */
        if (q == NULL || proc == NULL)
                return NULL;

        for (int i = 0; i < q->size; i++)
        {
                if (q->proc[i] == proc)
                {
                        struct pcb_t *result = q->proc[i];

                        /* Shift elements left to fill the gap. */
                        for (int j = i; j < q->size - 1; j++)
                                q->proc[j] = q->proc[j + 1];

                        q->size--;
                        return result;
                }
        }

        return NULL;
}
