#pragma once

#include <stddef.h>
#include <stdint.h>

struct HeapItem { 
    uint64_t val; // heap value, expiration time.
    size_t *ref; // point to entry's heap idx. 
};

void heap_update(HeapItem * a, size_t pos, size_t len);