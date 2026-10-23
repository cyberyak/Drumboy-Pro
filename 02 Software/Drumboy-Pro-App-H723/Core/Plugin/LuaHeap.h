#ifndef __LUA_HEAP_H
#define __LUA_HEAP_H

#include <stddef.h>
#include <stdint.h>

/* Lua Heap ---------------------------------------------------------*/
// Fixed memory pool in DTCM (otherwise unused by the stock firmware) shared
// by all Lua plugin states. First-fit, address-ordered free list with
// coalescing; sized for control-rate scripts, not for audio-rate use.

void luaHeap_initialize();
void* luaHeap_alloc(size_t size);
void luaHeap_free(void* ptr);
size_t luaHeap_blockSize(const void* ptr);  // Usable size of an allocated block
size_t luaHeap_getFree();
size_t luaHeap_getSize();

#endif
