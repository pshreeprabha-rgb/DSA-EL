#include "stack.h"
#include <stdlib.h>

#define STACK_INITIAL_CAPACITY 8

struct Stack {
    void **items;
    size_t size;
    size_t capacity;
    FreeFn free_fn;
};

Stack *stack_create(FreeFn free_fn) {
    Stack *s = (Stack *)malloc(sizeof(Stack));
    if (!s) {
        return NULL;
    }
    s->items = (void **)malloc(STACK_INITIAL_CAPACITY * sizeof(void *));
    if (!s->items) {
        free(s);
        return NULL;
    }
    s->size = 0;
    s->capacity = STACK_INITIAL_CAPACITY;
    s->free_fn = free_fn;
    return s;
}

void stack_destroy(Stack *s) {
    if (!s) {
        return;
    }
    stack_clear(s);
    free(s->items);
    free(s);
}

void stack_clear(Stack *s) {
    if (!s) {
        return;
    }
    if (s->free_fn) {
        for (size_t i = 0; i < s->size; i++) {
            if (s->items[i]) {
                s->free_fn(s->items[i]);
            }
        }
    }
    s->size = 0;
}

Status stack_push(Stack *s, void *item) {
    if (!s) {
        return STATUS_ERR_NULL;
    }
    if (s->size >= s->capacity) {
        size_t new_cap = s->capacity * 2;
        void **new_items = (void **)realloc(s->items, new_cap * sizeof(void *));
        if (!new_items) {
            return STATUS_ERR_NOMEM;
        }
        s->items = new_items;
        s->capacity = new_cap;
    }
    s->items[s->size++] = item;
    return STATUS_OK;
}

void *stack_pop(Stack *s) {
    if (!s || s->size == 0) {
        return NULL;
    }
    s->size--;
    return s->items[s->size];
}

void *stack_peek(const Stack *s) {
    if (!s || s->size == 0) {
        return NULL;
    }
    return s->items[s->size - 1];
}

size_t stack_size(const Stack *s) {
    return s ? s->size : 0;
}

int stack_is_empty(const Stack *s) {
    return (!s || s->size == 0) ? 1 : 0;
}

void *stack_get(const Stack *s, size_t index_from_top) {
    if (!s || index_from_top >= s->size) {
        return NULL;
    }
    return s->items[s->size - 1 - index_from_top];
}

