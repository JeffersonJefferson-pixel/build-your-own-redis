#pragma once

#include <string>
#include <vector>

#include "common.h"
#include "hashtable.h"
#include "zset.h"
#include "server_out.h"

enum {
  T_STR = 0, // string
  T_ZSET = 1, // sorted set
};

enum {
  ERR_UNKNOWN = 1,
  ERR_2BIG = 2,
  ERR_TYPE = 3,
  ERR_ARG = 4,
};

// structure for the key 
struct Entry {
  struct HNode node;
  std::string key;
  // for ttl.
  size_t heap_idx = -1; // index to the ttl heap.
  uint32_t type = 0;
  std::string val; // string 
  ZSet *zset = NULL; // sorted set
};


void do_keys(std::vector<std::string> &cmd, std::string &out);
void do_get(std::vector<std::string> &cmd, std::string &out);
void do_set(std::vector<std::string> &cmd, std::string &out);
void do_del(std::vector<std::string> cmd, std::string &out);
bool expect_zset(std::string &out, std::string &s, Entry **ent);
void do_zadd(std::vector<std::string> &cmd, std::string &out);
void do_zrem(std::vector<std::string> &cmd, std::string &out);
void do_zscore(std::vector<std::string> &cmd, std::string &out);
void do_zquery(std::vector<std::string> &cmd, std::string &out);
void do_expire(std::vector<std::string> &cmd, std::string &out);
void *begin_arr(std::string &out);
void end_arr(std::string &out, void *ctx, uint32_t n);
void heap_delete(std::vector<HeapItem> &a, size_t pos);
void heap_upsert(std::vector<HeapItem> &a, size_t pos, HeapItem t);
void entry_del(Entry *ent);