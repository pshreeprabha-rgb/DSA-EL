#include "linkedlist.h"
#include <stdlib.h>

typedef struct ListNode {
    void *item;
    struct ListNode *prev;
    struct ListNode *next;
} ListNode;

struct LinkedList {
    ListNode *head;
    ListNode *tail;
    size_t size;
    FreeFn free_fn;
};

LinkedList *ll_create(FreeFn free_fn) {
    LinkedList *l = (LinkedList *)malloc(sizeof(LinkedList));
    if (!l) {
        return NULL;
    }
    l->head = NULL;
    l->tail = NULL;
    l->size = 0;
    l->free_fn = free_fn;
    return l;
}

void ll_destroy(LinkedList *l) {
    if (!l) {
        return;
    }
    ll_clear(l);
    free(l);
}

void ll_clear(LinkedList *l) {
    if (!l) {
        return;
    }
    ListNode *curr = l->head;
    while (curr) {
        ListNode *next = curr->next;
        if (l->free_fn && curr->item) {
            l->free_fn(curr->item);
        }
        free(curr);
        curr = next;
    }
    l->head = NULL;
    l->tail = NULL;
    l->size = 0;
}

Status ll_append(LinkedList *l, void *item) {
    if (!l) {
        return STATUS_ERR_NULL;
    }
    ListNode *node = (ListNode *)malloc(sizeof(ListNode));
    if (!node) {
        return STATUS_ERR_NOMEM;
    }
    node->item = item;
    node->next = NULL;
    node->prev = l->tail;
    if (l->tail) {
        l->tail->next = node;
    } else {
        l->head = node;
    }
    l->tail = node;
    l->size++;
    return STATUS_OK;
}

Status ll_prepend(LinkedList *l, void *item) {
    if (!l) {
        return STATUS_ERR_NULL;
    }
    ListNode *node = (ListNode *)malloc(sizeof(ListNode));
    if (!node) {
        return STATUS_ERR_NOMEM;
    }
    node->item = item;
    node->prev = NULL;
    node->next = l->head;
    if (l->head) {
        l->head->prev = node;
    } else {
        l->tail = node;
    }
    l->head = node;
    l->size++;
    return STATUS_OK;
}

Status ll_insert_at(LinkedList *l, size_t index, void *item) {
    if (!l) {
        return STATUS_ERR_NULL;
    }
    if (index > l->size) {
        return STATUS_ERR_INVALID;
    }
    if (index == 0) {
        return ll_prepend(l, item);
    }
    if (index == l->size) {
        return ll_append(l, item);
    }

    ListNode *curr;
    if (index < l->size / 2) {
        curr = l->head;
        for (size_t i = 0; i < index; i++) {
            curr = curr->next;
        }
    } else {
        curr = l->tail;
        for (size_t i = l->size - 1; i > index; i--) {
            curr = curr->prev;
        }
    }

    ListNode *node = (ListNode *)malloc(sizeof(ListNode));
    if (!node) {
        return STATUS_ERR_NOMEM;
    }
    node->item = item;
    node->prev = curr->prev;
    node->next = curr;
    curr->prev->next = node;
    curr->prev = node;
    l->size++;
    return STATUS_OK;
}

Status ll_remove_at(LinkedList *l, size_t index) {
    if (!l) {
        return STATUS_ERR_NULL;
    }
    if (l->size == 0) {
        return STATUS_ERR_EMPTY;
    }
    if (index >= l->size) {
        return STATUS_ERR_INVALID;
    }

    ListNode *curr;
    if (index < l->size / 2) {
        curr = l->head;
        for (size_t i = 0; i < index; i++) {
            curr = curr->next;
        }
    } else {
        curr = l->tail;
        for (size_t i = l->size - 1; i > index; i--) {
            curr = curr->prev;
        }
    }

    if (curr->prev) {
        curr->prev->next = curr->next;
    } else {
        l->head = curr->next;
    }

    if (curr->next) {
        curr->next->prev = curr->prev;
    } else {
        l->tail = curr->prev;
    }

    l->size--;
    if (l->free_fn && curr->item) {
        l->free_fn(curr->item);
    }
    free(curr);
    return STATUS_OK;
}

size_t ll_remove_if(LinkedList *l, PredFn pred, const void *ctx) {
    if (!l || !pred) {
        return 0;
    }
    size_t count = 0;
    ListNode *curr = l->head;
    while (curr) {
        ListNode *next = curr->next;
        if (pred(curr->item, ctx)) {
            if (curr->prev) {
                curr->prev->next = curr->next;
            } else {
                l->head = curr->next;
            }

            if (curr->next) {
                curr->next->prev = curr->prev;
            } else {
                l->tail = curr->prev;
            }

            l->size--;
            if (l->free_fn && curr->item) {
                l->free_fn(curr->item);
            }
            free(curr);
            count++;
        }
        curr = next;
    }
    return count;
}

void *ll_get(const LinkedList *l, size_t index) {
    if (!l || index >= l->size) {
        return NULL;
    }
    ListNode *curr;
    if (index < l->size / 2) {
        curr = l->head;
        for (size_t i = 0; i < index; i++) {
            curr = curr->next;
        }
    } else {
        curr = l->tail;
        for (size_t i = l->size - 1; i > index; i--) {
            curr = curr->prev;
        }
    }
    return curr->item;
}

void *ll_find(const LinkedList *l, PredFn pred, const void *ctx) {
    if (!l || !pred) {
        return NULL;
    }
    for (ListNode *curr = l->head; curr; curr = curr->next) {
        if (pred(curr->item, ctx)) {
            return curr->item;
        }
    }
    return NULL;
}

void ll_foreach(const LinkedList *l, VisitFn fn, void *ctx) {
    if (!l || !fn) {
        return;
    }
    for (ListNode *curr = l->head; curr; curr = curr->next) {
        fn(curr->item, ctx);
    }
}

void ll_foreach_reverse(const LinkedList *l, VisitFn fn, void *ctx) {
    if (!l || !fn) {
        return;
    }
    for (ListNode *curr = l->tail; curr; curr = curr->prev) {
        fn(curr->item, ctx);
    }
}

size_t ll_size(const LinkedList *l) {
    return l ? l->size : 0;
}

int ll_is_empty(const LinkedList *l) {
    return (!l || l->size == 0) ? 1 : 0;
}

