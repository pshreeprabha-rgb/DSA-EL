#include "hashtable.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define HT_INITIAL_BUCKETS 16

typedef struct HtEntry {
    union {
        int64_t int_key;
        char *str_key;
    } key;
    void *value;
    struct HtEntry *next;
} HtEntry;

struct HashTable {
    HtKeyKind kind;
    HtEntry **buckets;
    size_t bucket_count;
    size_t size;
    FreeFn value_free_fn;
};

static inline uint64_t hash_int64(int64_t x) {
    uint64_t z = (uint64_t)x;
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static inline uint64_t hash_str(const char *str) {
    uint64_t hash = UINT64_C(14695981039346656037);
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        hash ^= (uint64_t)(*p++);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static char *ht_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, s, len + 1);
    return copy;
}

static Status ht_resize(HashTable *h) {
    size_t new_cap = h->bucket_count * 2;
    HtEntry **new_buckets = (HtEntry **)calloc(new_cap, sizeof(HtEntry *));
    if (!new_buckets) {
        return STATUS_ERR_NOMEM;
    }
    for (size_t i = 0; i < h->bucket_count; i++) {
        HtEntry *curr = h->buckets[i];
        while (curr) {
            HtEntry *next = curr->next;
            uint64_t hash = (h->kind == HT_KEY_INT)
                ? hash_int64(curr->key.int_key)
                : hash_str(curr->key.str_key);
            size_t idx = (size_t)(hash & (new_cap - 1));
            curr->next = new_buckets[idx];
            new_buckets[idx] = curr;
            curr = next;
        }
    }
    free(h->buckets);
    h->buckets = new_buckets;
    h->bucket_count = new_cap;
    return STATUS_OK;
}

HashTable *ht_create(HtKeyKind kind, FreeFn value_free_fn) {
    HashTable *h = (HashTable *)malloc(sizeof(HashTable));
    if (!h) {
        return NULL;
    }
    h->buckets = (HtEntry **)calloc(HT_INITIAL_BUCKETS, sizeof(HtEntry *));
    if (!h->buckets) {
        free(h);
        return NULL;
    }
    h->kind = kind;
    h->bucket_count = HT_INITIAL_BUCKETS;
    h->size = 0;
    h->value_free_fn = value_free_fn;
    return h;
}

void ht_destroy(HashTable *h) {
    if (!h) {
        return;
    }
    ht_clear(h);
    free(h->buckets);
    free(h);
}

void ht_clear(HashTable *h) {
    if (!h) {
        return;
    }
    for (size_t i = 0; i < h->bucket_count; i++) {
        HtEntry *curr = h->buckets[i];
        while (curr) {
            HtEntry *next = curr->next;
            if (h->kind == HT_KEY_STR && curr->key.str_key) {
                free(curr->key.str_key);
            }
            if (h->value_free_fn && curr->value) {
                h->value_free_fn(curr->value);
            }
            free(curr);
            curr = next;
        }
        h->buckets[i] = NULL;
    }
    h->size = 0;
}

Status ht_put_int(HashTable *h, int64_t key, void *value) {
    if (!h) {
        return STATUS_ERR_NULL;
    }
    if (h->kind != HT_KEY_INT) {
        return STATUS_ERR_INVALID;
    }

    uint64_t hash = hash_int64(key);
    size_t idx = (size_t)(hash & (h->bucket_count - 1));

    for (HtEntry *e = h->buckets[idx]; e; e = e->next) {
        if (e->key.int_key == key) {
            if (h->value_free_fn && e->value) {
                h->value_free_fn(e->value);
            }
            e->value = value;
            return STATUS_OK;
        }
    }

    if ((double)(h->size + 1) / (double)h->bucket_count > 0.75) {
        if (ht_resize(h) == STATUS_OK) {
            idx = (size_t)(hash & (h->bucket_count - 1));
        }
    }

    HtEntry *new_entry = (HtEntry *)malloc(sizeof(HtEntry));
    if (!new_entry) {
        return STATUS_ERR_NOMEM;
    }
    new_entry->key.int_key = key;
    new_entry->value = value;
    new_entry->next = h->buckets[idx];
    h->buckets[idx] = new_entry;
    h->size++;
    return STATUS_OK;
}

void *ht_get_int(const HashTable *h, int64_t key) {
    if (!h || h->kind != HT_KEY_INT) {
        return NULL;
    }
    uint64_t hash = hash_int64(key);
    size_t idx = (size_t)(hash & (h->bucket_count - 1));
    for (const HtEntry *e = h->buckets[idx]; e; e = e->next) {
        if (e->key.int_key == key) {
            return e->value;
        }
    }
    return NULL;
}

int ht_contains_int(const HashTable *h, int64_t key) {
    if (!h || h->kind != HT_KEY_INT) {
        return 0;
    }
    uint64_t hash = hash_int64(key);
    size_t idx = (size_t)(hash & (h->bucket_count - 1));
    for (const HtEntry *e = h->buckets[idx]; e; e = e->next) {
        if (e->key.int_key == key) {
            return 1;
        }
    }
    return 0;
}

Status ht_remove_int(HashTable *h, int64_t key) {
    if (!h) {
        return STATUS_ERR_NULL;
    }
    if (h->kind != HT_KEY_INT) {
        return STATUS_ERR_INVALID;
    }
    uint64_t hash = hash_int64(key);
    size_t idx = (size_t)(hash & (h->bucket_count - 1));

    HtEntry *curr = h->buckets[idx];
    HtEntry *prev = NULL;
    while (curr) {
        if (curr->key.int_key == key) {
            if (prev) {
                prev->next = curr->next;
            } else {
                h->buckets[idx] = curr->next;
            }
            if (h->value_free_fn && curr->value) {
                h->value_free_fn(curr->value);
            }
            free(curr);
            h->size--;
            return STATUS_OK;
        }
        prev = curr;
        curr = curr->next;
    }
    return STATUS_ERR_NOT_FOUND;
}

Status ht_put_str(HashTable *h, const char *key, void *value) {
    if (!h || !key) {
        return STATUS_ERR_NULL;
    }
    if (h->kind != HT_KEY_STR) {
        return STATUS_ERR_INVALID;
    }

    uint64_t hash = hash_str(key);
    size_t idx = (size_t)(hash & (h->bucket_count - 1));

    for (HtEntry *e = h->buckets[idx]; e; e = e->next) {
        if (strcmp(e->key.str_key, key) == 0) {
            if (h->value_free_fn && e->value) {
                h->value_free_fn(e->value);
            }
            e->value = value;
            return STATUS_OK;
        }
    }

    if ((double)(h->size + 1) / (double)h->bucket_count > 0.75) {
        if (ht_resize(h) == STATUS_OK) {
            idx = (size_t)(hash & (h->bucket_count - 1));
        }
    }

    char *key_copy = ht_strdup(key);
    if (!key_copy) {
        return STATUS_ERR_NOMEM;
    }

    HtEntry *new_entry = (HtEntry *)malloc(sizeof(HtEntry));
    if (!new_entry) {
        free(key_copy);
        return STATUS_ERR_NOMEM;
    }
    new_entry->key.str_key = key_copy;
    new_entry->value = value;
    new_entry->next = h->buckets[idx];
    h->buckets[idx] = new_entry;
    h->size++;
    return STATUS_OK;
}

void *ht_get_str(const HashTable *h, const char *key) {
    if (!h || !key || h->kind != HT_KEY_STR) {
        return NULL;
    }
    uint64_t hash = hash_str(key);
    size_t idx = (size_t)(hash & (h->bucket_count - 1));
    for (const HtEntry *e = h->buckets[idx]; e; e = e->next) {
        if (strcmp(e->key.str_key, key) == 0) {
            return e->value;
        }
    }
    return NULL;
}

int ht_contains_str(const HashTable *h, const char *key) {
    if (!h || !key || h->kind != HT_KEY_STR) {
        return 0;
    }
    uint64_t hash = hash_str(key);
    size_t idx = (size_t)(hash & (h->bucket_count - 1));
    for (const HtEntry *e = h->buckets[idx]; e; e = e->next) {
        if (strcmp(e->key.str_key, key) == 0) {
            return 1;
        }
    }
    return 0;
}

Status ht_remove_str(HashTable *h, const char *key) {
    if (!h || !key) {
        return STATUS_ERR_NULL;
    }
    if (h->kind != HT_KEY_STR) {
        return STATUS_ERR_INVALID;
    }
    uint64_t hash = hash_str(key);
    size_t idx = (size_t)(hash & (h->bucket_count - 1));

    HtEntry *curr = h->buckets[idx];
    HtEntry *prev = NULL;
    while (curr) {
        if (strcmp(curr->key.str_key, key) == 0) {
            if (prev) {
                prev->next = curr->next;
            } else {
                h->buckets[idx] = curr->next;
            }
            if (h->value_free_fn && curr->value) {
                h->value_free_fn(curr->value);
            }
            if (curr->key.str_key) {
                free(curr->key.str_key);
            }
            free(curr);
            h->size--;
            return STATUS_OK;
        }
        prev = curr;
        curr = curr->next;
    }
    return STATUS_ERR_NOT_FOUND;
}

size_t ht_size(const HashTable *h) {
    return h ? h->size : 0;
}

size_t ht_bucket_count(const HashTable *h) {
    return h ? h->bucket_count : 0;
}

void ht_foreach(const HashTable *h, VisitFn fn, void *ctx) {
    if (!h || !fn) {
        return;
    }
    for (size_t i = 0; i < h->bucket_count; i++) {
        for (const HtEntry *e = h->buckets[i]; e; e = e->next) {
            fn(e->value, ctx);
        }
    }
}

