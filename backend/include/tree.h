#ifndef TREE_H
#define TREE_H

#include <stddef.h>
#include "common.h"

typedef struct Tree Tree;
typedef struct TreeNode TreeNode;

/**
 * Creates a general tree with a single root node storing root_data.
 *
 * @param root_data Pointer to data stored in root node (may be NULL).
 * @param free_fn Optional callback to free node data.
 * @return Pointer to newly allocated Tree, or NULL on allocation failure.
 * Ownership: Caller owns the Tree. Tree owns root_data if free_fn is non-NULL.
 * Complexity: O(1)
 */
Tree *tree_create(void *root_data, FreeFn free_fn);

/**
 * Destroys the tree, freeing all nodes and contained data via free_fn if set.
 *
 * @param t Pointer to Tree (NULL-safe).
 * Ownership: Tree and all contained owned data are freed.
 * Complexity: O(n)
 */
void tree_destroy(Tree *t);

/**
 * Returns the root node of the tree.
 *
 * @param t Pointer to Tree.
 * @return Pointer to root TreeNode, or NULL if t is NULL.
 * Ownership: Node remains owned by tree.
 * Complexity: O(1)
 */
TreeNode *tree_root(const Tree *t);

/**
 * Adds a child node with the given data to the specified parent node.
 *
 * @param t Pointer to Tree.
 * @param parent Pointer to parent TreeNode.
 * @param data Data pointer for the new child node.
 * @return Pointer to newly created child TreeNode, or NULL on error or failure.
 * Ownership: Tree takes ownership of data if free_fn is non-NULL.
 * Complexity: O(k) where k is the number of existing children of parent.
 */
TreeNode *tree_add_child(Tree *t, TreeNode *parent, void *data);

/**
 * Removes a node and its entire subtree, freeing all nodes and owned data.
 *
 * @param t Pointer to Tree.
 * @param node Subtree root node to remove (cannot be tree root).
 * @return STATUS_OK on success, STATUS_ERR_NULL if t or node is NULL,
 *         STATUS_ERR_INVALID if node is the tree root.
 * Ownership: Removed nodes and their data (if free_fn set) are freed.
 * Complexity: O(s) where s is the size of the subtree.
 */
Status tree_remove_subtree(Tree *t, TreeNode *node);

/**
 * Returns the data stored in a tree node.
 *
 * @param n Pointer to TreeNode.
 * @return Pointer to data, or NULL if n is NULL.
 * Ownership: Data remains owned by tree.
 * Complexity: O(1)
 */
void *tree_node_data(const TreeNode *n);

/**
 * Returns the parent of a tree node.
 *
 * @param n Pointer to TreeNode.
 * @return Pointer to parent TreeNode, or NULL if n is root or NULL.
 * Ownership: Node remains owned by tree.
 * Complexity: O(1)
 */
TreeNode *tree_node_parent(const TreeNode *n);

/**
 * Returns the first child of a tree node.
 *
 * @param n Pointer to TreeNode.
 * @return Pointer to first child TreeNode, or NULL if none or n is NULL.
 * Ownership: Node remains owned by tree.
 * Complexity: O(1)
 */
TreeNode *tree_node_first_child(const TreeNode *n);

/**
 * Returns the next sibling of a tree node.
 *
 * @param n Pointer to TreeNode.
 * @return Pointer to next sibling TreeNode, or NULL if none or n is NULL.
 * Ownership: Node remains owned by tree.
 * Complexity: O(1)
 */
TreeNode *tree_node_next_sibling(const TreeNode *n);

/**
 * Returns the total number of nodes in the tree.
 *
 * @param t Pointer to Tree.
 * @return Total node count, or 0 if t is NULL.
 * Ownership: No change.
 * Complexity: O(1)
 */
size_t tree_count(const Tree *t);

/**
 * Returns the number of immediate children of a node.
 *
 * @param n Pointer to TreeNode.
 * @return Number of direct children, or 0 if n is NULL.
 * Ownership: No change.
 * Complexity: O(k) where k is the number of children.
 */
size_t tree_node_child_count(const TreeNode *n);

/**
 * Returns the height of the tree (number of edges on longest path from root to leaf).
 * A single root node has height 0.
 *
 * @param t Pointer to Tree.
 * @return Tree height, or 0 if t is NULL or tree has only root.
 * Ownership: No change.
 * Complexity: O(n)
 */
size_t tree_height(const Tree *t);

/**
 * Traverses the tree in preorder (node, then children), invoking fn on each node's data.
 *
 * @param t Pointer to Tree.
 * @param fn Visitor callback function.
 * @param ctx Context pointer passed to fn.
 * Ownership: Data remains owned by tree.
 * Complexity: O(n)
 */
void tree_preorder(const Tree *t, VisitFn fn, void *ctx);

/**
 * Traverses the tree in level-order (breadth-first), invoking fn on each node's data.
 *
 * @param t Pointer to Tree.
 * @param fn Visitor callback function.
 * @param ctx Context pointer passed to fn.
 * Ownership: Data remains owned by tree.
 * Complexity: O(n)
 */
void tree_levelorder(const Tree *t, VisitFn fn, void *ctx);

/**
 * Searches the tree in preorder for the first node whose data satisfies pred.
 *
 * @param t Pointer to Tree.
 * @param pred Predicate callback returning nonzero on match.
 * @param ctx Context pointer passed to pred.
 * @return Pointer to matching TreeNode, or NULL if not found or on error.
 * Ownership: Node remains owned by tree.
 * Complexity: O(n)
 */
TreeNode *tree_find(const Tree *t, PredFn pred, const void *ctx);

#endif /* TREE_H */

