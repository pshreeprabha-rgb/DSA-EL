#ifndef STACK_H
#define STACK_H

#include <stddef.h>
#include "common.h"

typedef struct Stack Stack;

/**
 * Creates an empty stack with initial capacity of 8.
 *
 * @param free_fn Optional callback to free items.
 * @return Pointer to newly allocated Stack, or NULL on allocation failure.
 * Ownership: Caller owns the returned Stack.
 * Complexity: O(1)
 */
Stack *stack_create(FreeFn free_fn);

/**
 * Destroys the stack, freeing all stored items via free_fn if set.
 *
 * @param s Pointer to Stack (NULL-safe).
 * Ownership: Stack and any remaining owned items are freed.
 * Complexity: O(n)
 */
void stack_destroy(Stack *s);

/**
 * Removes all items from the stack, calling free_fn on each item if set.
 *
 * @param s Pointer to Stack (NULL-safe).
 * Ownership: Contained items are freed if free_fn is non-NULL.
 * Complexity: O(n)
 */
void stack_clear(Stack *s);

/**
 * Pushes an item onto the top of the stack, doubling capacity if full.
 *
 * @param s Pointer to Stack.
 * @param item Pointer to item to push.
 * @return STATUS_OK on success, STATUS_ERR_NULL if s is NULL, STATUS_ERR_NOMEM on allocation failure.
 * Ownership: Stack takes ownership of item if free_fn is non-NULL.
 * Complexity: O(1) amortized
 */
Status stack_push(Stack *s, void *item);

/**
 * Pops and returns the top item from the stack.
 *
 * @param s Pointer to Stack.
 * @return Pointer to popped item, or NULL if stack is empty or s is NULL.
 * Ownership: Ownership of item is transferred to caller (free_fn is NOT called).
 * Complexity: O(1)
 */
void *stack_pop(Stack *s);

/**
 * Peeks at the top item without removing it.
 *
 * @param s Pointer to Stack.
 * @return Pointer to top item, or NULL if stack is empty or s is NULL.
 * Ownership: Item remains owned by stack.
 * Complexity: O(1)
 */
void *stack_peek(const Stack *s);

/**
 * Returns the number of items currently in the stack.
 *
 * @param s Pointer to Stack.
 * @return Item count, or 0 if s is NULL.
 * Ownership: No change.
 * Complexity: O(1)
 */
size_t stack_size(const Stack *s);

/**
 * Checks if the stack is empty.
 *
 * @param s Pointer to Stack.
 * @return 1 if s is NULL or empty, 0 otherwise.
 * Ownership: No change.
 * Complexity: O(1)
 */
int stack_is_empty(const Stack *s);

/**
 * Retrieves the item at the specified 0-based offset from the top (0 = top).
 *
 * @param s Pointer to Stack.
 * @param index_from_top Offset from top of stack (0 is top, 1 is one below top).
 * @return Pointer to item, or NULL if index_from_top >= size or s is NULL.
 * Ownership: Item remains owned by stack.
 * Complexity: O(1)
 */
void *stack_get(const Stack *s, size_t index_from_top);

#endif /* STACK_H */

