#ifndef QUEUE_H
#define QUEUE_H

#include <stddef.h>
#include "common.h"

typedef struct Queue Queue;

/**
 * Creates an empty queue implemented as a circular buffer with initial capacity 8.
 *
 * @param free_fn Optional callback to free items.
 * @return Pointer to newly allocated Queue, or NULL on allocation failure.
 * Ownership: Caller owns the returned Queue.
 * Complexity: O(1)
 */
Queue *queue_create(FreeFn free_fn);

/**
 * Destroys the queue, freeing all contained items via free_fn if set.
 *
 * @param q Pointer to Queue (NULL-safe).
 * Ownership: Queue and all remaining owned items are freed.
 * Complexity: O(n)
 */
void queue_destroy(Queue *q);

/**
 * Removes all items from the queue, freeing items via free_fn if set.
 *
 * @param q Pointer to Queue (NULL-safe).
 * Ownership: Contained items are freed if free_fn is non-NULL.
 * Complexity: O(n)
 */
void queue_clear(Queue *q);

/**
 * Adds an item to the back of the queue, resizing circular buffer if full.
 *
 * @param q Pointer to Queue.
 * @param item Item to enqueue.
 * @return STATUS_OK on success, STATUS_ERR_NULL if q is NULL, STATUS_ERR_NOMEM on failure.
 * Ownership: Queue takes ownership of item if free_fn is non-NULL.
 * Complexity: O(1) amortized
 */
Status queue_enqueue(Queue *q, void *item);

/**
 * Removes and returns the item at the front of the queue.
 *
 * @param q Pointer to Queue.
 * @return Pointer to dequeued item, or NULL if queue is empty or q is NULL.
 * Ownership: Ownership of item is transferred to caller (free_fn is NOT called).
 * Complexity: O(1)
 */
void *queue_dequeue(Queue *q);

/**
 * Peeks at the front item without removing it.
 *
 * @param q Pointer to Queue.
 * @return Pointer to front item, or NULL if queue is empty or q is NULL.
 * Ownership: Item remains owned by queue.
 * Complexity: O(1)
 */
void *queue_peek(const Queue *q);

/**
 * Returns the number of items in the queue.
 *
 * @param q Pointer to Queue.
 * @return Number of items, or 0 if q is NULL.
 * Ownership: No change.
 * Complexity: O(1)
 */
size_t queue_size(const Queue *q);

/**
 * Checks if the queue is empty.
 *
 * @param q Pointer to Queue.
 * @return 1 if q is NULL or empty, 0 otherwise.
 * Ownership: No change.
 * Complexity: O(1)
 */
int queue_is_empty(const Queue *q);

/**
 * Traverses items in order from front to back, invoking fn on each.
 *
 * @param q Pointer to Queue.
 * @param fn Visitor function callback.
 * @param ctx Context pointer passed to fn.
 * Ownership: Items remain owned by queue.
 * Complexity: O(n)
 */
void queue_foreach(const Queue *q, VisitFn fn, void *ctx);

#endif /* QUEUE_H */

