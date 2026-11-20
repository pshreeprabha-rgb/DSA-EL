#include "tree.h"
#include "queue.h"
#include <stdlib.h>

struct TreeNode {
    void *data;
    TreeNode *parent;
    TreeNode *first_child;
    TreeNode *next_sibling;
};

struct Tree {
    TreeNode *root;
    FreeFn free_fn;
    size_t count;
};

static void free_subtree_nodes(TreeNode *node, FreeFn free_fn) {
    if (!node) {
        return;
    }
    TreeNode *child = node->first_child;
    while (child) {
        TreeNode *next = child->next_sibling;
        free_subtree_nodes(child, free_fn);
        child = next;
    }
    if (free_fn && node->data) {
        free_fn(node->data);
    }
    free(node);
}

static size_t count_subtree_nodes(const TreeNode *node) {
    if (!node) {
        return 0;
    }
    size_t c = 1;
    for (TreeNode *child = node->first_child; child; child = child->next_sibling) {
        c += count_subtree_nodes(child);
    }
    return c;
}

static size_t node_height(const TreeNode *n) {
    if (!n || !n->first_child) {
        return 0;
    }
    size_t max_h = 0;
    for (TreeNode *c = n->first_child; c; c = c->next_sibling) {
        size_t h = node_height(c);
        if (h > max_h) {
            max_h = h;
        }
    }
    return 1 + max_h;
}

static void preorder_helper(TreeNode *n, VisitFn fn, void *ctx) {
    if (!n) {
        return;
    }
    fn(n->data, ctx);
    for (TreeNode *c = n->first_child; c; c = c->next_sibling) {
        preorder_helper(c, fn, ctx);
    }
}

static TreeNode *find_helper(TreeNode *n, PredFn pred, const void *ctx) {
    if (!n) {
        return NULL;
    }
    if (pred(n->data, ctx)) {
        return n;
    }
    for (TreeNode *c = n->first_child; c; c = c->next_sibling) {
        TreeNode *found = find_helper(c, pred, ctx);
        if (found) {
            return found;
        }
    }
    return NULL;
}

Tree *tree_create(void *root_data, FreeFn free_fn) {
    Tree *t = (Tree *)malloc(sizeof(Tree));
    if (!t) {
        return NULL;
    }
    TreeNode *root = (TreeNode *)malloc(sizeof(TreeNode));
    if (!root) {
        free(t);
        return NULL;
    }
    root->data = root_data;
    root->parent = NULL;
    root->first_child = NULL;
    root->next_sibling = NULL;

    t->root = root;
    t->free_fn = free_fn;
    t->count = 1;
    return t;
}

void tree_destroy(Tree *t) {
    if (!t) {
        return;
    }
    free_subtree_nodes(t->root, t->free_fn);
    free(t);
}

TreeNode *tree_root(const Tree *t) {
    return t ? t->root : NULL;
}

TreeNode *tree_add_child(Tree *t, TreeNode *parent, void *data) {
    if (!t || !parent) {
        return NULL;
    }
    TreeNode *child = (TreeNode *)malloc(sizeof(TreeNode));
    if (!child) {
        return NULL;
    }
    child->data = data;
    child->parent = parent;
    child->first_child = NULL;
    child->next_sibling = NULL;

    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        TreeNode *curr = parent->first_child;
        while (curr->next_sibling) {
            curr = curr->next_sibling;
        }
        curr->next_sibling = child;
    }
    t->count++;
    return child;
}

Status tree_remove_subtree(Tree *t, TreeNode *node) {
    if (!t || !node) {
        return STATUS_ERR_NULL;
    }
    if (node == t->root) {
        return STATUS_ERR_INVALID;
    }
    TreeNode *parent = node->parent;
    if (!parent) {
        return STATUS_ERR_INVALID;
    }
    if (parent->first_child == node) {
        parent->first_child = node->next_sibling;
    } else {
        TreeNode *prev = parent->first_child;
        while (prev && prev->next_sibling != node) {
            prev = prev->next_sibling;
        }
        if (!prev) {
            return STATUS_ERR_NOT_FOUND;
        }
        prev->next_sibling = node->next_sibling;
    }
    node->next_sibling = NULL;
    node->parent = NULL;

    size_t sub_count = count_subtree_nodes(node);
    free_subtree_nodes(node, t->free_fn);
    t->count -= sub_count;
    return STATUS_OK;
}

void *tree_node_data(const TreeNode *n) {
    return n ? n->data : NULL;
}

TreeNode *tree_node_parent(const TreeNode *n) {
    return n ? n->parent : NULL;
}

TreeNode *tree_node_first_child(const TreeNode *n) {
    return n ? n->first_child : NULL;
}

TreeNode *tree_node_next_sibling(const TreeNode *n) {
    return n ? n->next_sibling : NULL;
}

size_t tree_count(const Tree *t) {
    return t ? t->count : 0;
}

size_t tree_node_child_count(const TreeNode *n) {
    if (!n) {
        return 0;
    }
    size_t c = 0;
    for (TreeNode *child = n->first_child; child; child = child->next_sibling) {
        c++;
    }
    return c;
}

size_t tree_height(const Tree *t) {
    if (!t || !t->root) {
        return 0;
    }
    return node_height(t->root);
}

void tree_preorder(const Tree *t, VisitFn fn, void *ctx) {
    if (!t || !t->root || !fn) {
        return;
    }
    preorder_helper(t->root, fn, ctx);
}

void tree_levelorder(const Tree *t, VisitFn fn, void *ctx) {
    if (!t || !t->root || !fn) {
        return;
    }
    Queue *q = queue_create(NULL);
    if (!q) {
        return;
    }
    if (queue_enqueue(q, t->root) != STATUS_OK) {
        queue_destroy(q);
        return;
    }
    while (!queue_is_empty(q)) {
        TreeNode *curr = (TreeNode *)queue_dequeue(q);
        if (curr) {
            fn(curr->data, ctx);
            for (TreeNode *c = curr->first_child; c; c = c->next_sibling) {
                if (queue_enqueue(q, c) != STATUS_OK) {
                    /* If enqueue fails on nomem, abort queue */
                    queue_destroy(q);
                    return;
                }
            }
        }
    }
    queue_destroy(q);
}

TreeNode *tree_find(const Tree *t, PredFn pred, const void *ctx) {
    if (!t || !t->root || !pred) {
        return NULL;
    }
    return find_helper(t->root, pred, ctx);
}

