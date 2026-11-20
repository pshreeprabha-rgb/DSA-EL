#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>
#include "common.h"

typedef struct Heap Heap;

/**
 * Callback invoked whenever an item's position in the heap array changes.
 * Used for decrease-key operations where items track their array index.
 *
 * @param item Pointer to the item.
 * @param index Current 0-based array index of the item.
 */
typedef void (*HeapIndexFn)(void *item, size_t index);

/**
 * Creates an array-based binary heap with initial capacity 16.
 *
 * Comparator convention:
 *   cmp(a, b) < 0 indicates 'a' has HIGHER priority than 'b' (popped first).
 *   - Min-heap: ascending order comparator (returns <0 when a < b).
 *   - Max-heap: descending order comparator (returns <0 when a > b).
 *
 * @param cmp Required comparison function (must not be NULL).
 * @param free_fn Optional callback to free items on clear/destroy.
 * @param index_fn Optional callback invoked on item placement/swap (may be NULL).
 * @return Pointer to newly allocated Heap, or NULL on error.
 * Ownership: Caller owns the Heap.
 * Complexity: O(1)
 */
Heap *heap_create(CmpFn cmp, FreeFn free_fn, HeapIndexFn index_fn);

/**
 * Destroys the heap and frees its memory, calling free_fn on stored items if set.
 *
 * @param h Pointer to Heap (NULL-safe).
 * Ownership: Heap and all remaining owned items are freed.
 * Complexity: O(n)
 */
void heap_destroy(Heap *h);

/**
 * Clears all items from the heap, freeing items via free_fn if set.
 *
 * @param h Pointer to Heap (NULL-safe).
 * Ownership: Contained items are freed if free_fn is non-NULL.
 * Complexity: O(n)
 */
void heap_clear(Heap *h);

/**
 * Pushes an item onto the heap, restoring the heap property.
 *
 * @param h Pointer to Heap.
 * @param item Item to insert.
 * @return STATUS_OK on success, STATUS_ERR_NULL if h is NULL, STATUS_ERR_NOMEM on failure.
 * Ownership: Heap takes ownership if free_fn is non-NULL.
 * Complexity: O(log n)
 */
Status heap_push(Heap *h, void *item);

/**
 * Removes and returns the highest-priority item (root) from the heap.
 *
 * @param h Pointer to Heap.
 * @return Highest priority item, or NULL if heap is empty or h is NULL.
 * Ownership: Ownership of item is transferred to caller (free_fn is NOT called).
 * Complexity: O(log n)
 */
void *heap_pop(Heap *h);

/**
 * Returns the highest-priority item without removing it.
 *
 * @param h Pointer to Heap.
 * @return Highest priority item, or NULL if heap is empty or h is NULL.
 * Ownership: Item remains owned by heap.
 * Complexity: O(1)
 */
void *heap_peek(const Heap *h);

/**
 * Re-establishes the heap property after the priority of the item at index changed.
 * This implements decrease-key / increase-key.
 *
 * @param h Pointer to Heap.
 * @param index 0-based array index of the modified item.
 * @return STATUS_OK on success, STATUS_ERR_NULL if h is NULL,
 *         STATUS_ERR_INVALID if index >= size.
 * Ownership: No change.
 * Complexity: O(log n)
 */
Status heap_update(Heap *h, size_t index);

/**
 * Removes and returns the item at the specified index, restoring heap order.
 *
 * @param h Pointer to Heap.
 * @param index 0-based array index of item to remove.
 * @return Removed item, or NULL if h is NULL or index >= size.
 * Ownership: Ownership transferred to caller (free_fn is NOT called).
 * Complexity: O(log n)
 */
void *heap_remove_at(Heap *h, size_t index);

/**
 * Returns the number of items in the heap.
 *
 * @param h Pointer to Heap.
 * @return Item count, or 0 if h is NULL.
 * Ownership: No change.
 * Complexity: O(1)
 */
size_t heap_size(const Heap *h);

/**
 * Checks if the heap is empty.
 *
 * @param h Pointer to Heap.
 * @return 1 if h is NULL or empty, 0 otherwise.
 * Ownership: No change.
 * Complexity: O(1)
 */
int heap_is_empty(const Heap *h);

/**
 * Verifies that the heap ordering invariant holds for every node.
 *
 * @param h Pointer to Heap.
 * @return 1 if valid, 0 if invalid or h is NULL.
 * Ownership: No change.
 * Complexity: O(n)
 */
int heap_is_valid(const Heap *h);

#endif /* HEAP_H */

