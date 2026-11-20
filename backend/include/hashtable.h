#ifndef HASHTABLE_H
#define HASHTABLE_H

#include <stddef.h>
#include <stdint.h>
#include "common.h"

typedef enum HtKeyKind {
    HT_KEY_INT = 0,
    HT_KEY_STR = 1
} HtKeyKind;

typedef struct HashTable HashTable;

/**
 * Creates an empty hash table with separate chaining. Initial bucket count is 16.
 * Resizes (doubles and rehashes) when load factor (size / buckets) > 0.75.
 *
 * NOTE: NULL values are allowed (e.g. when using hash table as a set).
 * Therefore, ht_get_* returning NULL is ambiguous with "key not found".
 * Always use ht_contains_int / ht_contains_str to test membership.
 *
 * @param kind HT_KEY_INT for int64_t keys, or HT_KEY_STR for string keys.
 * @param value_free_fn Optional callback to free stored values.
 * @return Pointer to newly allocated HashTable, or NULL on error.
 * Ownership: Caller owns the table. String keys are copied and owned by table.
 * Complexity: O(1)
 */
HashTable *ht_create(HtKeyKind kind, FreeFn value_free_fn);

/**
 * Destroys the hash table, freeing all entries, string keys, and owned values.
 *
 * @param h Pointer to HashTable (NULL-safe).
 * Ownership: Table and all owned keys/values are freed.
 * Complexity: O(n)
 */
void ht_destroy(HashTable *h);

/**
 * Removes all entries, freeing string keys and values via value_free_fn if set.
 *
 * @param h Pointer to HashTable (NULL-safe).
 * Ownership: Contained values and string keys are freed.
 * Complexity: O(n)
 */
void ht_clear(HashTable *h);

/**
 * Inserts or replaces a key-value pair in an integer-key table.
 * If key already exists, replaces the value (freeing old value via value_free_fn if set).
 *
 * @param h Pointer to HashTable.
 * @param key int64_t key.
 * @param value Pointer to value (may be NULL for set usage).
 * @return STATUS_OK on success, STATUS_ERR_NULL if h is NULL,
 *         STATUS_ERR_INVALID if table is string-keyed, STATUS_ERR_NOMEM on failure.
 * Ownership: Table takes ownership of value if value_free_fn is non-NULL.
 * Complexity: O(1) average
 */
Status ht_put_int(HashTable *h, int64_t key, void *value);

/**
 * Retrieves the value associated with key in an integer-key table.
 *
 * @param h Pointer to HashTable.
 * @param key int64_t key.
 * @return Pointer to value, or NULL if absent or on error.
 * Ownership: Value remains owned by table.
 * Complexity: O(1) average
 */
void *ht_get_int(const HashTable *h, int64_t key);

/**
 * Tests whether key is present in an integer-key table.
 *
 * @param h Pointer to HashTable.
 * @param key int64_t key.
 * @return 1 if key exists, 0 otherwise.
 * Ownership: No change.
 * Complexity: O(1) average
 */
int ht_contains_int(const HashTable *h, int64_t key);

/**
 * Removes the entry for key, freeing value via value_free_fn if set.
 *
 * @param h Pointer to HashTable.
 * @param key int64_t key.
 * @return STATUS_OK on success, STATUS_ERR_NULL if h is NULL,
 *         STATUS_ERR_INVALID if table is string-keyed, STATUS_ERR_NOT_FOUND if absent.
 * Ownership: Stored value is freed via value_free_fn if set.
 * Complexity: O(1) average
 */
Status ht_remove_int(HashTable *h, int64_t key);

/**
 * Inserts or replaces a key-value pair in a string-key table.
 * The table creates its own strdup copy of key.
 * If key already exists, replaces value (freeing old value via value_free_fn if set).
 *
 * @param h Pointer to HashTable.
 * @param key NUL-terminated string key.
 * @param value Pointer to value (may be NULL for set usage).
 * @return STATUS_OK on success, STATUS_ERR_NULL if h or key is NULL,
 *         STATUS_ERR_INVALID if table is integer-keyed, STATUS_ERR_NOMEM on failure.
 * Ownership: Table takes ownership of value if value_free_fn is non-NULL.
 * Complexity: O(1) average
 */
Status ht_put_str(HashTable *h, const char *key, void *value);

/**
 * Retrieves the value associated with key in a string-key table.
 *
 * @param h Pointer to HashTable.
 * @param key NUL-terminated string key.
 * @return Pointer to value, or NULL if absent or on error.
 * Ownership: Value remains owned by table.
 * Complexity: O(1) average
 */
void *ht_get_str(const HashTable *h, const char *key);

/**
 * Tests whether key is present in a string-key table.
 *
 * @param h Pointer to HashTable.
 * @param key NUL-terminated string key.
 * @return 1 if key exists, 0 otherwise.
 * Ownership: No change.
 * Complexity: O(1) average
 */
int ht_contains_str(const HashTable *h, const char *key);

/**
 * Removes the entry for key, freeing value via value_free_fn and the key copy.
 *
 * @param h Pointer to HashTable.
 * @param key NUL-terminated string key.
 * @return STATUS_OK on success, STATUS_ERR_NULL if h or key is NULL,
 *         STATUS_ERR_INVALID if table is integer-keyed, STATUS_ERR_NOT_FOUND if absent.
 * Ownership: Stored value is freed via value_free_fn if set.
 * Complexity: O(1) average
 */
Status ht_remove_str(HashTable *h, const char *key);

/**
 * Returns the total number of key-value pairs stored in the table.
 *
 * @param h Pointer to HashTable.
 * @return Number of entries, or 0 if h is NULL.
 * Ownership: No change.
 * Complexity: O(1)
 */
size_t ht_size(const HashTable *h);

/**
 * Returns the current number of allocated buckets.
 *
 * @param h Pointer to HashTable.
 * @return Number of buckets, or 0 if h is NULL.
 * Ownership: No change.
 * Complexity: O(1)
 */
size_t ht_bucket_count(const HashTable *h);

/**
 * Traverses all stored values in unspecified order, invoking fn on each.
 *
 * @param h Pointer to HashTable.
 * @param fn Visitor callback function.
 * @param ctx Context pointer passed to fn.
 * Ownership: Values remain owned by table.
 * Complexity: O(buckets + size)
 */
void ht_foreach(const HashTable *h, VisitFn fn, void *ctx);

#endif /* HASHTABLE_H */

