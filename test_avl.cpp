#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <set> 
#include "test_common.h"
#include "avl.h"

static bool del(Container &c, uint32_t val) {
  AVLNode *cur = c.root;
  while (cur) {
    uint32_t node_val = container_of(cur, Data, node)->val;
    if (val == node_val) {
      break;
    }
    cur = val < node_val ? cur->left : cur->right;
  }
  if (!cur) {
    return false;
  }

  c.root = avl_del(cur);
  delete container_of(cur, Data, node);
  return true;
}


static void avl_verify(AVLNode *parent, AVLNode *node) {
  if (!node) {
    return;
  }

  avl_verify(node, node->left);
  avl_verify(node, node->right);
  // the parent pointer is correct
  assert(node->parent == parent);
  // the auxiliary data is correct
  assert(node->cnt == 1 + avl_cnt(node->left) + avl_cnt(node->right));
  uint32_t l = avl_depth(node->left);
  uint32_t r = avl_depth(node->right);
  assert(node->depth == 1 + max(l, r));
  // height invariant
  assert(l == r || l + 1 == r || l == r +1);
  // data is ordered
  uint32_t val = container_of(node, Data, node)->val;
  if (node->left) {
    assert(node->left->parent == node);
    assert(container_of(node->left, Data, node)-> val <= val);
  }
  if (node->right) {
    assert(node->right->parent == node);
    assert(container_of(node->right, Data, node)->val >= val);
  }
}

static void extract(AVLNode *node, std::multiset<uint32_t> &extracted) {
  if (!node) {
    return;
  }
  extract(node->left, extracted);
  extracted.insert(container_of(node, Data, node)->val);
  extract(node->right, extracted);
}

static void container_verify(Container &c, const std::multiset<uint32_t> &ref) {
  avl_verify(NULL, c.root);
  assert(avl_cnt(c.root) == ref.size());
  std::multiset<uint32_t> extracted;
  extract(c.root, extracted);
  assert(extracted == ref);
}

static void test_insert(uint32_t sz) {
  for (uint32_t val = 0; val < sz; ++val) {
    Container c;
    std::multiset<uint32_t> ref;
    // create tree of the given size
    for (uint32_t i = 0; i < sz; ++i) {
      if (i == val) {
        continue;
      }
      add(c, i);
      ref.insert(i);
    }
    container_verify(c, ref);
    // insert into the position
    add(c, val);
    ref.insert(val);
    container_verify(c, ref);
    dispose(c);
  }
}

static void test_insert_dup(uint32_t sz) {
  for (uint32_t val =  0; val < sz; ++val) {
    Container c;
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < sz; ++i) {
      add(c, i);
      ref.insert(i);
    }
    container_verify(c, ref);

    add(c, val);
    ref.insert(val);
    container_verify(c, ref);
    dispose(c);
  }
}

static void test_remove(uint32_t sz) {
  for (uint32_t val = 0; val < sz; ++val) {
    Container c;
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < sz; ++i) {
      add(c, i);
      ref.insert(i);
    }
    container_verify(c, ref);

    assert(del(c, val));
    ref.erase(val);
    container_verify(c, ref);
    dispose(c);
  }
}

int main() {
  Container c;

  // some quick tests
  container_verify(c, {});
  add(c, 123);
  container_verify(c, {123});
  assert(!del(c, 124));
  assert(del(c, 123));
  container_verify(c, {});

  // sequential insertion
  std::multiset<uint32_t> ref;
  for (uint32_t i = 0; i < 1000; i += 3) {
    add(c, i);
    ref.insert(i);
    container_verify(c, ref);
  }

  // random insertion
  for (uint32_t i = 0; i < 100; i++) {
    uint32_t val = (uint32_t)rand() % 1000;
    add(c, val);
    ref.insert(val);
    container_verify(c, ref);
  }

  // random deletion
  for (uint32_t i = 0; i < 200; i++) {
    uint32_t val = (uint32_t)rand() % 1000;
    auto it = ref.find(val);
    if (it == ref.end()) {
      assert(!del(c, val));
    } else {
      assert(del(c, val));
      ref.erase(it);
    }
    container_verify(c, ref);
  }

  // insertion/deletion at various positions
  for (uint32_t i = 0; i < 200; ++i) {
    test_insert(i);
    test_insert_dup(i);
    test_remove(i);
  }

  dispose(c);
  return 0;
}