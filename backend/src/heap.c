#include "heap.h"
#include <stdlib.h>

#define HEAP_INITIAL_CAPACITY 16

struct Heap {
    void **items;
    size_t size;
    size_t capacity;
    CmpFn cmp;
    FreeFn free_fn;
    HeapIndexFn index_fn;
};

static inline void set_item(Heap *h, size_t idx, void *item) {
    h->items[idx] = item;
    if (h->index_fn && item) {
        h->index_fn(item, idx);
    }
}

static inline void swap_items(Heap *h, size_t i, size_t j) {
    void *tmp = h->items[i];
    h->items[i] = h->items[j];
    h->items[j] = tmp;
    if (h->index_fn) {
        if (h->items[i]) {
            h->index_fn(h->items[i], i);
        }
        if (h->items[j]) {
            h->index_fn(h->items[j], j);
        }
    }
}

static void sift_up(Heap *h, size_t i) {
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (h->cmp(h->items[i], h->items[parent]) < 0) {
            swap_items(h, i, parent);
            i = parent;
        } else {
            break;
        }
    }
}

static void sift_down(Heap *h, size_t i) {
    while (2 * i + 1 < h->size) {
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;
        size_t best = i;

        if (h->cmp(h->items[left], h->items[best]) < 0) {
            best = left;
        }
        if (right < h->size && h->cmp(h->items[right], h->items[best]) < 0) {
            best = right;
        }
        if (best != i) {
            swap_items(h, i, best);
            i = best;
        } else {
            break;
        }
    }
}

Heap *heap_create(CmpFn cmp, FreeFn free_fn, HeapIndexFn index_fn) {
    if (!cmp) {
        return NULL;
    }
    Heap *h = (Heap *)malloc(sizeof(Heap));
    if (!h) {
        return NULL;
    }
    h->items = (void **)malloc(HEAP_INITIAL_CAPACITY * sizeof(void *));
    if (!h->items) {
        free(h);
        return NULL;
    }
    h->size = 0;
    h->capacity = HEAP_INITIAL_CAPACITY;
    h->cmp = cmp;
    h->free_fn = free_fn;
    h->index_fn = index_fn;
    return h;
}

void heap_destroy(Heap *h) {
    if (!h) {
        return;
    }
    heap_clear(h);
    free(h->items);
    free(h);
}

void heap_clear(Heap *h) {
    if (!h) {
        return;
    }
    if (h->free_fn) {
        for (size_t i = 0; i < h->size; i++) {
            if (h->items[i]) {
                h->free_fn(h->items[i]);
            }
        }
    }
    h->size = 0;
}

Status heap_push(Heap *h, void *item) {
    if (!h) {
        return STATUS_ERR_NULL;
    }
    if (h->size >= h->capacity) {
        size_t new_cap = h->capacity * 2;
        void **new_items = (void **)realloc(h->items, new_cap * sizeof(void *));
        if (!new_items) {
            return STATUS_ERR_NOMEM;
        }
        h->items = new_items;
        h->capacity = new_cap;
    }
    size_t idx = h->size;
    h->size++;
    set_item(h, idx, item);
    sift_up(h, idx);
    return STATUS_OK;
}

void *heap_pop(Heap *h) {
    if (!h || h->size == 0) {
        return NULL;
    }
    void *root = h->items[0];
    h->size--;
    if (h->size > 0) {
        set_item(h, 0, h->items[h->size]);
        sift_down(h, 0);
    }
    return root;
}

void *heap_peek(const Heap *h) {
    if (!h || h->size == 0) {
        return NULL;
    }
    return h->items[0];
}

Status heap_update(Heap *h, size_t index) {
    if (!h) {
        return STATUS_ERR_NULL;
    }
    if (index >= h->size) {
        return STATUS_ERR_INVALID;
    }
    if (index > 0 && h->cmp(h->items[index], h->items[(index - 1) / 2]) < 0) {
        sift_up(h, index);
    } else {
        sift_down(h, index);
    }
    return STATUS_OK;
}

void *heap_remove_at(Heap *h, size_t index) {
    if (!h || index >= h->size) {
        return NULL;
    }
    void *removed = h->items[index];
    h->size--;
    if (index < h->size) {
        set_item(h, index, h->items[h->size]);
        heap_update(h, index);
    }
    return removed;
}

size_t heap_size(const Heap *h) {
    return h ? h->size : 0;
}

int heap_is_empty(const Heap *h) {
    return (!h || h->size == 0) ? 1 : 0;
}

int heap_is_valid(const Heap *h) {
    if (!h) {
        return 0;
    }
    for (size_t i = 0; i < h->size; i++) {
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;

        if (left < h->size && h->cmp(h->items[left], h->items[i]) < 0) {
            return 0;
        }
        if (right < h->size && h->cmp(h->items[right], h->items[i]) < 0) {
            return 0;
        }
    }
    return 1;
}

