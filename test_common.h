#pragma once

#include "common.h"
#include "avl.h"

struct Data {
  AVLNode node;
  uint32_t val = 0;
};

struct Container {
  AVLNode *root = NULL;
};

static void add(Container &c, uint32_t val) {
  Data *data = new Data();
  avl_init(&data->node);
  data->val = val;

  AVLNode *cur = NULL; // current node
  AVLNode **from = &c.root; // incoming pointer to the next node
  // tree search
  while (*from) {
    cur = *from;
    uint32_t node_val = container_of(cur, Data, node)->val;
    from = (val < node_val) ? &cur->left : &cur->right;
  }
  *from = &data->node; // attach the new node
  data->node.parent = cur;
  c.root = avl_fix(&data->node);
}

static void dispose(Container &c) {
  while (c.root) {
    AVLNode *node = c.root;
    c.root = avl_del(c.root);
    delete container_of(node, Data, node);
  }
}

static void dispose(AVLNode *node) {
    if (node) {
        dispose(node->left);
        dispose(node->right);
        delete container_of(node, Data, node);
    }
}