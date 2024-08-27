
#include <math.h>
#include "server_data.h"

// data structure for the key space
HMap db;

static void h_scan(HTab *tab, void (*f)(HNode *, void *), void *arg) {
  if (tab->size == 0) {
    return;
  }
  for (size_t i = 0; i < tab->mask + 1; ++i) {
    HNode *node = tab->tab[i];
    while (node) {
      f(node, arg);
      node = node->next;
    }
  }
}

static bool entry_eq(HNode *lhs, HNode *rhs) {
  struct Entry *le = container_of(lhs, struct Entry, node);
  struct Entry *re = container_of(rhs, struct Entry, node);
  return le->key == re->key;
}

static void cb_scan(HNode *node, void *arg) {
  std::string &out = *(std::string *)arg;
  out_str(out, container_of(node, Entry, node)->key);
}

static bool str2dbl(const std::string &s, double &out) {
  char *endp = NULL;
  out = strtod(s.c_str(), &endp);
  return endp == s.c_str() + s.size() && !isnan(out);
}

static bool str2int(const std::string &s, int64_t &out) {
  char *endp = NULL;
  out = strtoll(s.c_str(), &endp, 10);
  return endp == s.c_str() + s.size();
}


void do_keys(std::vector<std::string> &cmd, std::string &out) {
  (void)cmd;
  out_arr(out, (uint32_t)hm_size(&db));
  h_scan(&db.ht1, &cb_scan, &out);
  h_scan(&db.ht2, &cb_scan, &out);
}

void do_get(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&db, &key.node, &entry_eq);
  if (!node) {
    return out_nil(out);
  }

  
  Entry *ent = container_of(node, Entry, node);
  if (ent->type != T_STR) {
    return out_err(out, ERR_TYPE, "expect string type");
  }
  const std::string &val = container_of(node, Entry, node)->val;
  out_str(out, val);
}

void do_set(std::vector<std::string> &cmd, std::string &out) {
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&db, &key.node, &entry_eq);
  if (node) {
    Entry *ent = container_of(node, Entry, node);
    if (ent->type != T_STR) {
      return out_err(out, ERR_TYPE, "expect string type");
    }
    ent->val.swap(cmd[2]);
  } else {
    Entry *ent = new Entry();
    ent->key.swap(key.key);
    ent->node.hcode = key.node.hcode;
    ent->val.swap(cmd[2]);
    hm_insert(&db, &ent->node);
  }

  out_nil(out);
}

static void entry_del(Entry *ent) {
  switch (ent->type) {
    case T_ZSET:
      zset_dispose(ent->zset);
      delete ent->zset;
      break;
  }
  delete ent;
}

void do_del(std::vector<std::string> cmd, std::string &out) {
  Entry  key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_pop(&db, &key.node, &entry_eq);
  if (node) {
    entry_del(container_of(node, Entry, node));
  }

  out_int(out, node ? 1 : 0);
}

bool expect_zset(std::string &out, std::string &s, Entry **ent) {
  Entry key;
  key.key.swap(s);
  key.node.hcode = str_hash((uint8_t *) key.key.data(), key.key.size());
  HNode *hnode = hm_lookup(&db, &key.node, &entry_eq);
  if (!hnode) {
    out_nil(out);
    return false;
  } 

  *ent = container_of(hnode, Entry, node);
  if ((*ent)->type != T_ZSET) {
    out_err(out, ERR_TYPE, "expect zset");
    return false;
  }
  return true;
}

// zadd zset score name
void do_zadd(std::vector<std::string> &cmd, std::string &out) {
  double score = 0;
  if (!str2dbl(cmd[2], score)) {
    return out_err(out, ERR_ARG, "expect fp number");
  }

  // look up or create the zset
  Entry key;
  key.key.swap(cmd[1]);
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *hnode = hm_lookup(&db, &key.node, &entry_eq);

  Entry *ent = NULL;
  if (!hnode) {
    ent = new Entry();
    ent->key.swap(key.key);
    ent->node.hcode = key.node.hcode;
    ent->type = T_ZSET;
    ent->zset = new ZSet();
    hm_insert(&db, &ent->node);
  } else {
    ent = container_of(hnode, Entry, node);
    if (ent->type != T_ZSET) {
      return out_err(out, ERR_TYPE, "expect zset");
    }
  }

  // add or udpate the tuple
  const std::string &name = cmd[3];
  bool added = zset_add(ent->zset, name.data(), name.size(), score);
  return out_int(out, (int64_t)added);
}

// zrem zset name
void do_zrem(std::vector<std::string> &cmd, std::string &out) {
  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    return;
  }

  const std::string &name = cmd[2];
  ZNode *znode = zset_pop(ent->zset, name.data(), name.size());
  if (znode) {
    znode_del(znode);
  }
  return out_int(out, znode ? 1 : 0);
}

//zscore zset name
void do_zscore(std::vector<std::string> &cmd, std::string &out) {
  Entry *ent = NULL;
  if (!expect_zset(out, cmd[1], &ent)) {
    return;
  }

  const std::string &name = cmd[2];
  ZNode *znode = zset_lookup(ent->zset, name.data(), name.size());
  return znode ? out_dbl(out, znode->score) : out_nil(out);
}

// zquery key score name offset limit
void do_zquery(std::vector<std::string> &cmd, std::string &out) {
    // parse args
    double score = 0;
    if (!str2dbl(cmd[2], score)) {
      return out_err(out, ERR_ARG, "expect fp number");
    }

    const std::string &name = cmd[3];
    int64_t offset = 0;
    int64_t limit = 0;
    if (!str2int(cmd[4], offset)) {
      return out_err(out, ERR_ARG, "exoect int");
    }
    if (!str2int(cmd[5], limit)) {
      return out_err(out, ERR_ARG, "expect int");
    }

    // get the zset
    Entry *ent = NULL;
    if (!expect_zset(out, cmd[1], &ent)) {
      if (out[0] == SER_NIL) {
        out.clear();
        out_arr(out, 0);
      }
      return;
    }

    if (limit <= 0) {
      return out_arr(out, 0);
    }
    
    // 1. seek
    ZNode *znode = zset_query(ent->zset, score, name.data(), name.size());
    // 2. offste
    znode = znode_offset(znode, offset);
    // 3. iterate and output
    void *arr = begin_arr(out);
    uint32_t n = 0;
    while (znode && (int64_t)n < limit) {
        out_str(out, znode->name, znode->len);
        out_dbl(out, znode->score);
        znode = znode_offset(znode, +1); // successor
        n += 2;
    }
    end_arr(out, arr, n);
}