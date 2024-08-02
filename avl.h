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

AVLNode *avl_offset(AVLNode *node, int64_t offset);