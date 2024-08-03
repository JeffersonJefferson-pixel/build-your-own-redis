#pragma once

#include <stddef.h>
#include <stdint.h>

struct AVLNode {
    uint32_t depth = 0; // subtree height
    uint32_t cnt = 0; // subtree size;
    AVLNode *left = NULL;
    AVLNode *right = NULL;
    AVLNode *parent = NULL;
};

inline void avl_init(AVLNode *node) {
    node->depth = 1;
    node->cnt = 1;
    node->left = node->right = node->parent = NULL;
}

uint32_t avl_depth(AVLNode *node);
uint32_t avl_cnt(AVLNode *node);
AVLNode *avl_offset(AVLNode *node, int64_t offset);
AVLNode *avl_fix(AVLNode *node);
AVLNode *avl_del(AVLNode *node);