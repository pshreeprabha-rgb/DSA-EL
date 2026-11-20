#ifndef LINKEDLIST_H
#define LINKEDLIST_H

#include <stddef.h>
#include "common.h"

typedef struct LinkedList LinkedList;

/**
 * Creates a new doubly linked list.
 *
 * @param free_fn Optional callback to free items. If non-NULL, list owns items.
 * @return Pointer to newly allocated LinkedList, or NULL on allocation failure.
 * Ownership: Caller owns the returned LinkedList.
 * Complexity: O(1)
 */
LinkedList *ll_create(FreeFn free_fn);

/**
 * Destroys the linked list and frees all nodes and owned items.
 *
 * @param l Pointer to LinkedList (NULL-safe).
 * Ownership: Destroys the list and all contained items if free_fn is non-NULL.
 * Complexity: O(n)
 */
void ll_destroy(LinkedList *l);

/**
 * Removes all elements from the list, freeing items if free_fn is set.
 *
 * @param l Pointer to LinkedList (NULL-safe).
 * Ownership: Contained items are freed if free_fn was provided at creation.
 * Complexity: O(n)
 */
void ll_clear(LinkedList *l);

/**
 * Appends an item to the end of the list.
 *
 * @param l Pointer to LinkedList.
 * @param item Item to append (may be NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL if l is NULL, STATUS_ERR_NOMEM on failure.
 * Ownership: List takes ownership if free_fn is set.
 * Complexity: O(1)
 */
Status ll_append(LinkedList *l, void *item);

/**
 * Prepends an item to the beginning of the list.
 *
 * @param l Pointer to LinkedList.
 * @param item Item to prepend (may be NULL).
 * @return STATUS_OK on success, STATUS_ERR_NULL if l is NULL, STATUS_ERR_NOMEM on failure.
 * Ownership: List takes ownership if free_fn is set.
 * Complexity: O(1)
 */
Status ll_prepend(LinkedList *l, void *item);

/**
 * Inserts an item at the specified 0-based index.
 *
 * @param l Pointer to LinkedList.
 * @param index Insertion index (0 <= index <= size). index == size appends.
 * @param item Item to insert.
 * @return STATUS_OK on success, STATUS_ERR_NULL if l is NULL,
 *         STATUS_ERR_INVALID if index > size, STATUS_ERR_NOMEM on allocation failure.
 * Ownership: List takes ownership if free_fn is set.
 * Complexity: O(n) worst case, O(1) for index == 0 or index == size.
 */
Status ll_insert_at(LinkedList *l, size_t index, void *item);

/**
 * Removes the item at the specified 0-based index, freeing it via free_fn if set.
 *
 * @param l Pointer to LinkedList.
 * @param index Index of item to remove (0 <= index < size).
 * @return STATUS_OK on success, STATUS_ERR_NULL if l is NULL,
 *         STATUS_ERR_EMPTY if list is empty, STATUS_ERR_INVALID if index >= size.
 * Ownership: Item is freed by list if free_fn is set; otherwise discarded.
 * Complexity: O(n) worst case, O(1) for head or tail.
 */
Status ll_remove_at(LinkedList *l, size_t index);

/**
 * Removes all items matching the predicate, freeing each via free_fn if set.
 *
 * @param l Pointer to LinkedList.
 * @param pred Predicate function returning nonzero for elements to remove.
 * @param ctx Context pointer passed to pred.
 * @return Number of elements removed.
 * Ownership: Removed items are freed by list if free_fn is set.
 * Complexity: O(n)
 */
size_t ll_remove_if(LinkedList *l, PredFn pred, const void *ctx);

/**
 * Retrieves the item at the specified 0-based index without removing it.
 *
 * @param l Pointer to LinkedList.
 * @param index Index to retrieve.
 * @return Pointer to item, or NULL if l is NULL or index >= size.
 * Ownership: Item remains owned by list.
 * Complexity: O(n) worst case, O(1) for index == 0 or index == size - 1.
 */
void *ll_get(const LinkedList *l, size_t index);

/**
 * Finds the first item matching the predicate.
 *
 * @param l Pointer to LinkedList.
 * @param pred Predicate function returning nonzero on match.
 * @param ctx Context pointer passed to pred.
 * @return Pointer to matching item, or NULL if not found or on error.
 * Ownership: Item remains owned by list.
 * Complexity: O(n)
 */
void *ll_find(const LinkedList *l, PredFn pred, const void *ctx);

/**
 * Traverses the list forward (head to tail), invoking fn on each item.
 *
 * @param l Pointer to LinkedList.
 * @param fn Visitor callback function.
 * @param ctx Context pointer passed to fn.
 * Ownership: Items remain owned by list.
 * Complexity: O(n)
 */
void ll_foreach(const LinkedList *l, VisitFn fn, void *ctx);

/**
 * Traverses the list backward (tail to head), invoking fn on each item.
 *
 * @param l Pointer to LinkedList.
 * @param fn Visitor callback function.
 * @param ctx Context pointer passed to fn.
 * Ownership: Items remain owned by list.
 * Complexity: O(n)
 */
void ll_foreach_reverse(const LinkedList *l, VisitFn fn, void *ctx);

/**
 * Returns the number of elements in the list.
 *
 * @param l Pointer to LinkedList.
 * @return Number of elements, or 0 if l is NULL.
 * Ownership: No change.
 * Complexity: O(1)
 */
size_t ll_size(const LinkedList *l);

/**
 * Checks if the list is empty.
 *
 * @param l Pointer to LinkedList.
 * @return 1 if list is NULL or empty, 0 otherwise.
 * Ownership: No change.
 * Complexity: O(1)
 */
int ll_is_empty(const LinkedList *l);

#endif /* LINKEDLIST_H */

