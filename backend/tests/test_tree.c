#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_util.h"
#include "tree.h"

static int g_tree_free_count = 0;

static void tree_item_free(void *item) {
    if (item) {
        g_tree_free_count++;
        free(item);
    }
}

typedef struct TraversalCtx {
    char names[32][32];
    size_t count;
} TraversalCtx;

static void collect_tree_names(void *item, void *ctx) {
    TraversalCtx *c = (TraversalCtx *)ctx;
    if (item && c->count < 32) {
        strncpy(c->names[c->count], (const char *)item, 31);
        c->names[c->count][31] = '\0';
        c->count++;
    }
}

static int name_matcher(const void *item, const void *ctx) {
    if (!item || !ctx) return 0;
    return (strcmp((const char *)item, (const char *)ctx) == 0);
}

static void test_tree_null_args(void) {
    tree_destroy(NULL);
    TEST_ASSERT_NULL(tree_root(NULL));
    TEST_ASSERT_NULL(tree_add_child(NULL, NULL, NULL));
    TEST_ASSERT_EQ_INT(tree_remove_subtree(NULL, NULL), STATUS_ERR_NULL);
    TEST_ASSERT_NULL(tree_node_data(NULL));
    TEST_ASSERT_NULL(tree_node_parent(NULL));
    TEST_ASSERT_NULL(tree_node_first_child(NULL));
    TEST_ASSERT_NULL(tree_node_next_sibling(NULL));
    TEST_ASSERT_EQ_INT(tree_count(NULL), 0);
    TEST_ASSERT_EQ_INT(tree_node_child_count(NULL), 0);
    TEST_ASSERT_EQ_INT(tree_height(NULL), 0);
    tree_preorder(NULL, collect_tree_names, NULL);
    tree_levelorder(NULL, collect_tree_names, NULL);
    TEST_ASSERT_NULL(tree_find(NULL, name_matcher, NULL));
}

static void test_tree_single_node(void) {
    char root_val[] = "Bangalore";
    Tree *t = tree_create(root_val, NULL);
    TEST_ASSERT_NOT_NULL(t);

    TreeNode *r = tree_root(t);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQ_INT(strcmp((char *)tree_node_data(r), "Bangalore"), 0);
    TEST_ASSERT_NULL(tree_node_parent(r));
    TEST_ASSERT_NULL(tree_node_first_child(r));
    TEST_ASSERT_NULL(tree_node_next_sibling(r));
    TEST_ASSERT_EQ_INT(tree_count(t), 1);
    TEST_ASSERT_EQ_INT(tree_node_child_count(r), 0);
    TEST_ASSERT_EQ_INT(tree_height(t), 0);

    /* Removing root is STATUS_ERR_INVALID */
    TEST_ASSERT_EQ_INT(tree_remove_subtree(t, r), STATUS_ERR_INVALID);
    TEST_ASSERT_EQ_INT(tree_count(t), 1);

    tree_destroy(t);
}

static void test_tree_hierarchy_and_removal(void) {
    /* Build hierarchy:
     * City
     *   -> Depot1
     *        -> Route1A -> Bus1A1, Bus1A2
     *        -> Route1B -> Bus1B1, Bus1B2
     *   -> Depot2
     *        -> Route2A -> Bus2A1, Bus2A2
     *        -> Route2B -> Bus2B1, Bus2B2
     *   -> Depot3
     *        -> Route3A -> Bus3A1, Bus3A2
     *        -> Route3B -> Bus3B1, Bus3B2
     */
    char city[] = "City";
    char d1[] = "D1", d2[] = "D2", d3[] = "D3";
    char r1a[] = "R1A", r1b[] = "R1B", r2a[] = "R2A", r2b[] = "R2B", r3a[] = "R3A", r3b[] = "R3B";
    char b1a1[] = "B1A1", b1a2[] = "B1A2", b1b1[] = "B1B1", b1b2[] = "B1B2";
    char b2a1[] = "B2A1", b2a2[] = "B2A2", b2b1[] = "B2B1", b2b2[] = "B2B2";
    char b3a1[] = "B3A1", b3a2[] = "B3A2", b3b1[] = "B3B1", b3b2[] = "B3B2";

    Tree *t = tree_create(city, NULL);
    TreeNode *root = tree_root(t);

    TreeNode *nd1 = tree_add_child(t, root, d1);
    TreeNode *nd2 = tree_add_child(t, root, d2);
    TreeNode *nd3 = tree_add_child(t, root, d3);

    TEST_ASSERT_EQ_INT(tree_node_child_count(root), 3);
    TEST_ASSERT_EQ_INT(tree_height(t), 1);

    TreeNode *nr1a = tree_add_child(t, nd1, r1a);
    TreeNode *nr1b = tree_add_child(t, nd1, r1b);
    TreeNode *nr2a = tree_add_child(t, nd2, r2a);
    TreeNode *nr2b = tree_add_child(t, nd2, r2b);
    TreeNode *nr3a = tree_add_child(t, nd3, r3a);
    TreeNode *nr3b = tree_add_child(t, nd3, r3b);

    TEST_ASSERT_EQ_INT(tree_height(t), 2);

    tree_add_child(t, nr1a, b1a1);
    tree_add_child(t, nr1a, b1a2);
    tree_add_child(t, nr1b, b1b1);
    tree_add_child(t, nr1b, b1b2);

    tree_add_child(t, nr2a, b2a1);
    tree_add_child(t, nr2a, b2a2);
    tree_add_child(t, nr2b, b2b1);
    tree_add_child(t, nr2b, b2b2);

    tree_add_child(t, nr3a, b3a1);
    tree_add_child(t, nr3a, b3a2);
    tree_add_child(t, nr3b, b3b1);
    tree_add_child(t, nr3b, b3b2);

    /* Total nodes: 1 + 3 + 6 + 12 = 22 */
    TEST_ASSERT_EQ_INT(tree_count(t), 22);
    TEST_ASSERT_EQ_INT(tree_height(t), 3);

    /* Accessors */
    TEST_ASSERT_NOT_NULL(tree_node_first_child(root));
    TEST_ASSERT_EQ_INT(strcmp((char *)tree_node_data(tree_node_first_child(root)), "D1"), 0);
    TEST_ASSERT_NOT_NULL(tree_node_next_sibling(nd1));
    TEST_ASSERT_EQ_INT(strcmp((char *)tree_node_data(tree_node_next_sibling(nd1)), "D2"), 0);
    TEST_ASSERT_EQ_INT(strcmp((char *)tree_node_data(tree_node_parent(nd1)), "City"), 0);

    /* Preorder traversal */
    TraversalCtx pre_ctx = { {{0}}, 0 };
    tree_preorder(t, collect_tree_names, &pre_ctx);
    TEST_ASSERT_EQ_INT(pre_ctx.count, 22);
    TEST_ASSERT_EQ_INT(strcmp(pre_ctx.names[0], "City"), 0);
    TEST_ASSERT_EQ_INT(strcmp(pre_ctx.names[1], "D1"), 0);
    TEST_ASSERT_EQ_INT(strcmp(pre_ctx.names[2], "R1A"), 0);
    TEST_ASSERT_EQ_INT(strcmp(pre_ctx.names[3], "B1A1"), 0);

    /* Level-order traversal */
    TraversalCtx lvl_ctx = { {{0}}, 0 };
    tree_levelorder(t, collect_tree_names, &lvl_ctx);
    TEST_ASSERT_EQ_INT(lvl_ctx.count, 22);
    TEST_ASSERT_EQ_INT(strcmp(lvl_ctx.names[0], "City"), 0);
    TEST_ASSERT_EQ_INT(strcmp(lvl_ctx.names[1], "D1"), 0);
    TEST_ASSERT_EQ_INT(strcmp(lvl_ctx.names[2], "D2"), 0);
    TEST_ASSERT_EQ_INT(strcmp(lvl_ctx.names[3], "D3"), 0);

    /* Search with tree_find */
    char target[] = "B2B2";
    TreeNode *found_node = tree_find(t, name_matcher, target);
    TEST_ASSERT_NOT_NULL(found_node);
    TEST_ASSERT_EQ_INT(strcmp((char *)tree_node_data(found_node), "B2B2"), 0);

    char missing[] = "NonExistent";
    TEST_ASSERT_NULL(tree_find(t, name_matcher, missing));

    /* Remove subtree nd2 (Depot 2): contains D2, R2A, R2B, B2A1, B2A2, B2B1, B2B2 (7 nodes) */
    TEST_ASSERT_EQ_INT(tree_remove_subtree(t, nd2), STATUS_OK);
    TEST_ASSERT_EQ_INT(tree_count(t), 15);
    TEST_ASSERT_EQ_INT(tree_node_child_count(root), 2);
    /* nd1's next sibling should now be nd3 */
    TEST_ASSERT_EQ_INT(strcmp((char *)tree_node_data(tree_node_next_sibling(nd1)), "D3"), 0);
    /* Target from removed subtree should not be found */
    TEST_ASSERT_NULL(tree_find(t, name_matcher, target));

    tree_destroy(t);
}

static void test_tree_ownership(void) {
    g_tree_free_count = 0;
    int *root_val = (int *)malloc(sizeof(int));
    *root_val = 1;
    Tree *t = tree_create(root_val, tree_item_free);
    TEST_ASSERT_NOT_NULL(t);

    TreeNode *root = tree_root(t);
    int *c1 = (int *)malloc(sizeof(int)); *c1 = 2;
    int *c2 = (int *)malloc(sizeof(int)); *c2 = 3;
    TreeNode *nc1 = tree_add_child(t, root, c1);
    tree_add_child(t, root, c2);

    int *gc1 = (int *)malloc(sizeof(int)); *gc1 = 4;
    tree_add_child(t, nc1, gc1);

    TEST_ASSERT_EQ_INT(tree_count(t), 4);
    TEST_ASSERT_EQ_INT(g_tree_free_count, 0);

    /* Remove nc1 subtree (nc1 and gc1): 2 items freed */
    TEST_ASSERT_EQ_INT(tree_remove_subtree(t, nc1), STATUS_OK);
    TEST_ASSERT_EQ_INT(g_tree_free_count, 2);
    TEST_ASSERT_EQ_INT(tree_count(t), 2);

    /* Destroy tree: root and c2 freed (2 items) */
    tree_destroy(t);
    TEST_ASSERT_EQ_INT(g_tree_free_count, 4);
}

int main(void) {
    RUN_TEST(test_tree_null_args);
    RUN_TEST(test_tree_single_node);
    RUN_TEST(test_tree_hierarchy_and_removal);
    RUN_TEST(test_tree_ownership);
    return test_summary();
}

