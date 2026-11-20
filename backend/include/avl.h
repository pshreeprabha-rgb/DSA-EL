#ifndef AVL_H
#define AVL_H

#include <stddef.h>
#include "common.h"

typedef struct Avl Avl;

/**
 * Creates an empty self-balancing AVL tree.
 *
 * @param cmp Comparison function (must not be NULL). Returns <0, 0, >0.
 * @param free_fn Optional callback to free stored items on deletion/destroy.
 * @return Pointer to newly allocated Avl, or NULL on error.
 * Ownership: Caller owns the Avl tree.
 * Complexity: O(1)
 */
Avl *avl_create(CmpFn cmp, FreeFn free_fn);

/**
 * Destroys the AVL tree, freeing all nodes and contained items via free_fn if set.
 *
 * @param t Pointer to Avl (NULL-safe).
 * Ownership: Tree and all remaining owned items are freed.
 * Complexity: O(n)
 */
void avl_destroy(Avl *t);

/**
 * Inserts an item into the AVL tree, performing rotations if needed to rebalance.
 * Rejects duplicate keys (cmp == 0) with STATUS_ERR_DUPLICATE.
 *
 * @param t Pointer to Avl.
 * @param item Pointer to item to insert.
 * @return STATUS_OK on success, STATUS_ERR_NULL if t is NULL,
 *         STATUS_ERR_DUPLICATE if key already exists, STATUS_ERR_NOMEM on failure.
 * Ownership: Tree takes ownership of item if free_fn is non-NULL.
 * Complexity: O(log n)
 */
Status avl_insert(Avl *t, void *item);

/**
 * Deletes the item matching probe from the AVL tree and rebalances.
 *
 * @param t Pointer to Avl.
 * @param probe Pointer to probe object compared via cmp.
 * @return STATUS_OK on success, STATUS_ERR_NULL if t or probe is NULL,
 *         STATUS_ERR_NOT_FOUND if matching item is absent.
 * Ownership: The stored item is freed via free_fn if set.
 * Complexity: O(log n)
 */
Status avl_delete(Avl *t, const void *probe);

/**
 * Searches the AVL tree for an item matching probe.
 *
 * @param t Pointer to Avl.
 * @param probe Probe object compared via cmp.
 * @return Pointer to matching stored item, or NULL if not found or t is NULL.
 * Ownership: Item remains owned by tree.
 * Complexity: O(log n)
 */
void *avl_search(const Avl *t, const void *probe);

/**
 * Returns the minimum (smallest) item in the tree.
 *
 * @param t Pointer to Avl.
 * @return Smallest item, or NULL if tree is empty or t is NULL.
 * Ownership: Item remains owned by tree.
 * Complexity: O(log n)
 */
void *avl_min(const Avl *t);

/**
 * Returns the maximum (largest) item in the tree.
 *
 * @param t Pointer to Avl.
 * @return Largest item, or NULL if tree is empty or t is NULL.
 * Ownership: Item remains owned by tree.
 * Complexity: O(log n)
 */
void *avl_max(const Avl *t);

/**
 * Finds the smallest item greater than or equal to probe (lower bound).
 * Used for schedule timetable lookups (e.g. next departure after time T).
 *
 * @param t Pointer to Avl.
 * @param probe Probe object.
 * @return Pointer to smallest item >= probe, or NULL if none or on error.
 * Ownership: Item remains owned by tree.
 * Complexity: O(log n)
 */
void *avl_lower_bound(const Avl *t, const void *probe);

/**
 * Traverses the tree in-order (ascending key order), invoking fn on each item.
 *
 * @param t Pointer to Avl.
 * @param fn Visitor callback function.
 * @param ctx Context pointer passed to fn.
 * Ownership: Items remain owned by tree.
 * Complexity: O(n)
 */
void avl_inorder(const Avl *t, VisitFn fn, void *ctx);

/**
 * Returns the number of items stored in the tree.
 *
 * @param t Pointer to Avl.
 * @return Number of items, or 0 if t is NULL.
 * Ownership: No change.
 * Complexity: O(1)
 */
size_t avl_size(const Avl *t);

/**
 * Returns the height of the AVL tree. Empty tree is 0, single node is 1.
 *
 * @param t Pointer to Avl.
 * @return Tree height, or 0 if t is NULL or empty.
 * Ownership: No change.
 * Complexity: O(1)
 */
int avl_height(const Avl *t);

/**
 * Verifies BST order, node heights, and balance factor (|bf| <= 1) at all nodes.
 *
 * @param t Pointer to Avl.
 * @return 1 if balanced and valid, 0 otherwise.
 * Ownership: No change.
 * Complexity: O(n)
 */
int avl_is_balanced(const Avl *t);

#endif /* AVL_H */

