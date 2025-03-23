#pragma once;

#include "hashtable.h"
#include <vector>
#include "linked_list.h"
#include "heap.h"
#include "server_conn.h"

static struct {
    // data structure for the key space
    HMap db;
    // a map of all client connections, keyed by fd
    std::vector<Conn *> fd2conn;
    // timeers for idle connections
    DList idle_list;
    // timers for ttls.
    std::vector<HeapItem> heap;
} g_data;

static uint64_t get_monotonic_msec() {
    timespec tv = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return uint64_t(tv.tv_sec) * 1000 + tv.tv_nsec / 1000 / 1000;
}