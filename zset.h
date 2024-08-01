#pragma once

#include "hashtable.cpp"
#include "avl.cpp"

struct ZSet {
    AVLNode *tree = NULL;
    HMap hmap;
};

struct ZNode {
    AVLNode tree; // index by (score, name)
    HNode hmap; // index by name
    double score = 0;
    size_t len = 0;
    char name[0]; // variable length 
};

ZNode *zset_lookup(ZSet *zset, const char *name, size_t len);