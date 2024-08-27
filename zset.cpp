#include <stdlib.h>
#include <string.h>
#include <string>
#include "common.h"
#include "zset.h"

static ZNode *znode_new(const char *name, size_t len, double score) {
    ZNode *node = (ZNode *)malloc(sizeof(ZNode) + len);
    avl_init(&node->tree);
    node->hmap.next = NULL;
    node->hmap.hcode = str_hash((uint8_t *)name, len);
    node->score = score;
    node->len = len;
    memcpy(&node->name[0], name, len);
    return node;
}

struct HKey {
    HNode node;
    const char *name = NULL;
    size_t len = 0;
};

static bool hcmp(HNode *node, HNode *key) {
    ZNode *znode = container_of(node, ZNode, hmap);
    HKey *hkey = container_of(key, HKey, node);
    if (znode->len != hkey->len) {
        return false;
    } 
    return 0 == memcmp(znode->name, hkey->name, znode->len);
}


// lookup by name
ZNode *zset_lookup(ZSet *zset, const char *name, size_t len) {
    if (!zset->tree) {
        return NULL;
    }
    HKey key;
    key.node.hcode = str_hash((uint8_t *)name, len);
    key.name = name;
    key.len = len;
    HNode *found = hm_lookup(&zset->hmap, &key.node, &hcmp);
    return found ? container_of(found, ZNode, hmap) : NULL;
}

// compare by (score, name)
static bool zless(AVLNode *lhs, double score, const char *name, size_t len) {
    ZNode *node = container_of(lhs, ZNode, tree);
    if (node->score != score) {
        return node->score < score;
    }
    int rv = memcmp(node->name, name, min(node->len, len));
    if (rv != 0) {
        return rv < 0;
    }
    return node->len < len;
}

static bool zless(AVLNode *lhs, AVLNode *rhs) {
    ZNode *node = container_of(rhs, ZNode, tree);
    return zless(lhs, node->score, node->name, node->len);
}

// insert into the avl 
static void tree_add(ZSet *zset, ZNode *node) {
    AVLNode *cur = NULL; // current node
    AVLNode **from = &zset->tree; // incoming point to the next node
    while (*from) { // tree search
        cur = *from;
        from = zless(&node->tree, cur) ? &cur->left : &cur->right;
    }
    *from = &node->tree; // attach a new node
    node->tree.parent = cur;
    zset->tree = avl_fix(&node->tree);
}

// update the score of an existing node
static void zset_update(ZSet *zset, ZNode *node, double score) {
    if (node->score == score) {
        return;
    }
    // delete and re-insert
    zset->tree = avl_del(&node->tree);
    node->score = score;
    avl_init(&node->tree);
    tree_add(zset, node);
}

// add a new (score, name)
bool zset_add(ZSet *zset, const char *name, size_t len, double score) {
    ZNode *node = zset_lookup(zset, name, len);
    if (node) {
        // update the score of an existing pair
        zset_update(zset, node, score);
        return false;
    } else {
        // add a new ndoe
        node = znode_new(name, len, score);
        hm_insert(&zset->hmap, &node->hmap);
        tree_add(zset, node);
        return true;
    }
}

// delete

// lookup and detach a node by name
ZNode *zset_pop(ZSet *zset, const char *name, size_t len) {
    if (!zset->tree) {
        return NULL;
    }

    HKey key;
    key.node.hcode = str_hash((uint8_t *)name, len);
    key.name = name;
    key.len = len;
    HNode *found = hm_pop(&zset->hmap, &key.node, &hcmp);
    if (!found) {
        return NULL;
    }

    ZNode *node = container_of(found, ZNode, hmap);
    zset->tree = avl_del(&node->tree);
    return node;
};

// deallocate the node
void znode_del(ZNode *node) {
    free(node);
};

// range query
ZNode *zset_query(ZSet *zset, double score, const char *name, size_t len) {
    AVLNode *found = NULL;
    for (AVLNode *cur = zset->tree; cur;) {
        if (zless(cur, score, name, len)) {
            cur = cur->right;
        } else {
            found = cur; // candidate
            cur = cur->left;
        }
    }
    return found ? container_of(found, ZNode, tree) : NULL;
}

// offset into the succeeding of preceding node
ZNode *znode_offset(ZNode *node, int64_t offset) {
    // walk to the n-th succesesor/predecessor (offset)
    AVLNode *tnode = node ? avl_offset(&node->tree, offset) : NULL;
    return tnode ? container_of(tnode, ZNode, tree) : NULL;
}

static void tree_dispose(AVLNode *node) {
    if (!node) {
        return;
    }
    tree_dispose(node->left);
    tree_dispose(node->right);
    znode_del(container_of(node, ZNode, tree));
}

// destroy zset
void zset_dispose(ZSet *zset) {
    tree_dispose(zset->tree);
    hm_destroy(&zset->hmap);
}