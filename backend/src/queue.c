#include "queue.h"
#include <stdlib.h>

#define QUEUE_INITIAL_CAPACITY 8

struct Queue {
    void **items;
    size_t head;
    size_t size;
    size_t capacity;
    FreeFn free_fn;
};

Queue *queue_create(FreeFn free_fn) {
    Queue *q = (Queue *)malloc(sizeof(Queue));
    if (!q) {
        return NULL;
    }
    q->items = (void **)malloc(QUEUE_INITIAL_CAPACITY * sizeof(void *));
    if (!q->items) {
        free(q);
        return NULL;
    }
    q->head = 0;
    q->size = 0;
    q->capacity = QUEUE_INITIAL_CAPACITY;
    q->free_fn = free_fn;
    return q;
}

void queue_destroy(Queue *q) {
    if (!q) {
        return;
    }
    queue_clear(q);
    free(q->items);
    free(q);
}

void queue_clear(Queue *q) {
    if (!q) {
        return;
    }
    if (q->free_fn) {
        for (size_t i = 0; i < q->size; i++) {
            size_t idx = (q->head + i) % q->capacity;
            if (q->items[idx]) {
                q->free_fn(q->items[idx]);
            }
        }
    }
    q->head = 0;
    q->size = 0;
}

Status queue_enqueue(Queue *q, void *item) {
    if (!q) {
        return STATUS_ERR_NULL;
    }
    if (q->size >= q->capacity) {
        size_t new_cap = q->capacity * 2;
        void **new_items = (void **)malloc(new_cap * sizeof(void *));
        if (!new_items) {
            return STATUS_ERR_NOMEM;
        }
        for (size_t i = 0; i < q->size; i++) {
            new_items[i] = q->items[(q->head + i) % q->capacity];
        }
        free(q->items);
        q->items = new_items;
        q->head = 0;
        q->capacity = new_cap;
    }
    size_t tail = (q->head + q->size) % q->capacity;
    q->items[tail] = item;
    q->size++;
    return STATUS_OK;
}

void *queue_dequeue(Queue *q) {
    if (!q || q->size == 0) {
        return NULL;
    }
    void *item = q->items[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->size--;
    return item;
}

void *queue_peek(const Queue *q) {
    if (!q || q->size == 0) {
        return NULL;
    }
    return q->items[q->head];
}

size_t queue_size(const Queue *q) {
    return q ? q->size : 0;
}

int queue_is_empty(const Queue *q) {
    return (!q || q->size == 0) ? 1 : 0;
}

void queue_foreach(const Queue *q, VisitFn fn, void *ctx) {
    if (!q || !fn) {
        return;
    }
    for (size_t i = 0; i < q->size; i++) {
        size_t idx = (q->head + i) % q->capacity;
        fn(q->items[idx], ctx);
    }
}

