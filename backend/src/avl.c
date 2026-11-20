#include "avl.h"
#include <stdlib.h>

typedef struct AvlNode {
    void *item;
    struct AvlNode *left;
    struct AvlNode *right;
    int height;
} AvlNode;

struct Avl {
    AvlNode *root;
    CmpFn cmp;
    FreeFn free_fn;
    size_t size;
};

static inline int node_h(const AvlNode *n) {
    return n ? n->height : 0;
}

static inline int max_int(int a, int b) {
    return (a > b) ? a : b;
}

static inline void update_height(AvlNode *n) {
    if (n) {
        n->height = 1 + max_int(node_h(n->left), node_h(n->right));
    }
}

static inline int balance_factor(const AvlNode *n) {
    return n ? (node_h(n->left) - node_h(n->right)) : 0;
}

static AvlNode *rotate_right(AvlNode *y) {
    AvlNode *x = y->left;
    AvlNode *T2 = x->right;

    x->right = y;
    y->left = T2;

    update_height(y);
    update_height(x);

    return x;
}

static AvlNode *rotate_left(AvlNode *x) {
    AvlNode *y = x->right;
    AvlNode *T2 = y->left;

    y->left = x;
    x->right = T2;

    update_height(x);
    update_height(y);

    return y;
}

static AvlNode *rebalance(AvlNode *node) {
    if (!node) {
        return NULL;
    }
    update_height(node);
    int bf = balance_factor(node);

    /* Left Heavy */
    if (bf > 1) {
        /* LR case */
        if (balance_factor(node->left) < 0) {
            node->left = rotate_left(node->left);
        }
        /* LL case */
        return rotate_right(node);
    }

    /* Right Heavy */
    if (bf < -1) {
        /* RL case */
        if (balance_factor(node->right) > 0) {
            node->right = rotate_right(node->right);
        }
        /* RR case */
        return rotate_left(node);
    }

    return node;
}

static AvlNode *insert_node(AvlNode *node, void *item, CmpFn cmp, Status *status) {
    if (!node) {
        AvlNode *new_node = (AvlNode *)malloc(sizeof(AvlNode));
        if (!new_node) {
            *status = STATUS_ERR_NOMEM;
            return NULL;
        }
        new_node->item = item;
        new_node->left = NULL;
        new_node->right = NULL;
        new_node->height = 1;
        *status = STATUS_OK;
        return new_node;
    }

    int c = cmp(item, node->item);
    if (c == 0) {
        *status = STATUS_ERR_DUPLICATE;
        return node;
    } else if (c < 0) {
        AvlNode *new_left = insert_node(node->left, item, cmp, status);
        if (*status != STATUS_OK && *status != STATUS_ERR_DUPLICATE) {
            return node;
        }
        node->left = new_left;
    } else {
        AvlNode *new_right = insert_node(node->right, item, cmp, status);
        if (*status != STATUS_OK && *status != STATUS_ERR_DUPLICATE) {
            return node;
        }
        node->right = new_right;
    }

    if (*status != STATUS_OK) {
        return node;
    }

    return rebalance(node);
}

static AvlNode *delete_min_node(AvlNode *node, AvlNode **min_out) {
    if (!node->left) {
        *min_out = node;
        return node->right;
    }
    node->left = delete_min_node(node->left, min_out);
    return rebalance(node);
}

static AvlNode *delete_node(AvlNode *node, const void *probe, CmpFn cmp, FreeFn free_fn, Status *status) {
    if (!node) {
        *status = STATUS_ERR_NOT_FOUND;
        return NULL;
    }

    int c = cmp(probe, node->item);
    if (c < 0) {
        node->left = delete_node(node->left, probe, cmp, free_fn, status);
    } else if (c > 0) {
        node->right = delete_node(node->right, probe, cmp, free_fn, status);
    } else {
        *status = STATUS_OK;
        if (!node->left) {
            AvlNode *right_child = node->right;
            if (free_fn && node->item) {
                free_fn(node->item);
            }
            free(node);
            return right_child;
        } else if (!node->right) {
            AvlNode *left_child = node->left;
            if (free_fn && node->item) {
                free_fn(node->item);
            }
            free(node);
            return left_child;
        } else {
            AvlNode *succ = NULL;
            if (free_fn && node->item) {
                free_fn(node->item);
            }
            node->right = delete_min_node(node->right, &succ);
            node->item = succ->item;
            free(succ);
        }
    }

    return rebalance(node);
}

static void destroy_nodes(AvlNode *n, FreeFn free_fn) {
    if (!n) {
        return;
    }
    destroy_nodes(n->left, free_fn);
    destroy_nodes(n->right, free_fn);
    if (free_fn && n->item) {
        free_fn(n->item);
    }
    free(n);
}

static void inorder_helper(AvlNode *n, VisitFn fn, void *ctx) {
    if (!n) {
        return;
    }
    inorder_helper(n->left, fn, ctx);
    fn(n->item, ctx);
    inorder_helper(n->right, fn, ctx);
}

static int check_balanced(const AvlNode *n, CmpFn cmp, const AvlNode **prev) {
    if (!n) {
        return 1;
    }
    if (!check_balanced(n->left, cmp, prev)) {
        return 0;
    }

    if (*prev != NULL && cmp((*prev)->item, n->item) >= 0) {
        return 0;
    }
    *prev = n;

    if (!check_balanced(n->right, cmp, prev)) {
        return 0;
    }

    int lh = node_h(n->left);
    int rh = node_h(n->right);
    if (n->height != 1 + max_int(lh, rh)) {
        return 0;
    }

    int bf = lh - rh;
    if (bf < -1 || bf > 1) {
        return 0;
    }

    return 1;
}

Avl *avl_create(CmpFn cmp, FreeFn free_fn) {
    if (!cmp) {
        return NULL;
    }
    Avl *t = (Avl *)malloc(sizeof(Avl));
    if (!t) {
        return NULL;
    }
    t->root = NULL;
    t->cmp = cmp;
    t->free_fn = free_fn;
    t->size = 0;
    return t;
}

void avl_destroy(Avl *t) {
    if (!t) {
        return;
    }
    destroy_nodes(t->root, t->free_fn);
    free(t);
}

Status avl_insert(Avl *t, void *item) {
    if (!t) {
        return STATUS_ERR_NULL;
    }
    Status s = STATUS_OK;
    t->root = insert_node(t->root, item, t->cmp, &s);
    if (s == STATUS_OK) {
        t->size++;
    }
    return s;
}

Status avl_delete(Avl *t, const void *probe) {
    if (!t || !probe) {
        return STATUS_ERR_NULL;
    }
    Status s = STATUS_OK;
    t->root = delete_node(t->root, probe, t->cmp, t->free_fn, &s);
    if (s == STATUS_OK) {
        t->size--;
    }
    return s;
}

void *avl_search(const Avl *t, const void *probe) {
    if (!t || !t->root || !probe) {
        return NULL;
    }
    AvlNode *curr = t->root;
    while (curr) {
        int c = t->cmp(probe, curr->item);
        if (c == 0) {
            return curr->item;
        } else if (c < 0) {
            curr = curr->left;
        } else {
            curr = curr->right;
        }
    }
    return NULL;
}

void *avl_min(const Avl *t) {
    if (!t || !t->root) {
        return NULL;
    }
    AvlNode *curr = t->root;
    while (curr->left) {
        curr = curr->left;
    }
    return curr->item;
}

void *avl_max(const Avl *t) {
    if (!t || !t->root) {
        return NULL;
    }
    AvlNode *curr = t->root;
    while (curr->right) {
        curr = curr->right;
    }
    return curr->item;
}

void *avl_lower_bound(const Avl *t, const void *probe) {
    if (!t || !t->root || !probe) {
        return NULL;
    }
    AvlNode *curr = t->root;
    AvlNode *candidate = NULL;
    while (curr) {
        int c = t->cmp(curr->item, probe);
        if (c >= 0) {
            candidate = curr;
            curr = curr->left;
        } else {
            curr = curr->right;
        }
    }
    return candidate ? candidate->item : NULL;
}

void avl_inorder(const Avl *t, VisitFn fn, void *ctx) {
    if (!t || !t->root || !fn) {
        return;
    }
    inorder_helper(t->root, fn, ctx);
}

size_t avl_size(const Avl *t) {
    return t ? t->size : 0;
}

int avl_height(const Avl *t) {
    return t ? node_h(t->root) : 0;
}

int avl_is_balanced(const Avl *t) {
    if (!t) {
        return 0;
    }
    if (!t->root) {
        return 1;
    }
    const AvlNode *prev = NULL;
    return check_balanced(t->root, t->cmp, &prev);
}

