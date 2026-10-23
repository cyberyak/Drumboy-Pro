#include "Plugin.h"

#ifdef DRUMBOY_PLUGINS

#include "LuaHeap.h"

// clang-format off

const size_t kLuaHeapSize                           = 96 * 1024;        // Of 128 KB DTCM; the rest stays free for future native modules
const size_t kLuaHeapAlign                          = 8;

// clang-format on

/* Block Layout -----------------------------------------------------*/
// Every block starts with an 8-byte header holding its total size (header
// included). Free blocks also store the next free block right after it.

struct BlockHeader {
    uint32_t size;
    uint32_t reserved;
};

struct FreeBlock {
    BlockHeader header;
    FreeBlock* next;
};

const size_t kHeaderSize = sizeof(BlockHeader);
const size_t kMinBlockSize = (sizeof(FreeBlock) + kLuaHeapAlign - 1) & ~(kLuaHeapAlign - 1);  // Must hold a FreeBlock, multiple of 8

#ifndef LUA_HEAP_SECTION
#define LUA_HEAP_SECTION __attribute__((section(".DTCMRAM")))  // Overridable for host-side tests
#endif

static uint8_t heap[kLuaHeapSize] LUA_HEAP_SECTION __attribute__((aligned(8)));
static FreeBlock* freeList = nullptr;
static size_t freeBytes = 0;

static inline size_t alignUp(size_t size) {
    return (size + kLuaHeapAlign - 1) & ~(kLuaHeapAlign - 1);
}

void luaHeap_initialize() {
    /// @brief Turns the whole pool into one free block. DTCM is not zeroed at boot, so nothing else is assumed.

    freeList = reinterpret_cast<FreeBlock*>(heap);
    freeList->header.size = kLuaHeapSize;
    freeList->next = nullptr;
    freeBytes = kLuaHeapSize;
}

void* luaHeap_alloc(size_t size) {
    /// @brief First-fit allocation; splits the block when the remainder is large enough.

    if (size == 0) return nullptr;
    size_t needed = alignUp(size + kHeaderSize);
    if (needed < kMinBlockSize) needed = kMinBlockSize;

    FreeBlock** link = &freeList;
    while (*link != nullptr) {
        FreeBlock* block = *link;
        if (block->header.size >= needed) {
            // 1. SPLIT OR TAKE WHOLE
            if (block->header.size - needed >= kMinBlockSize) {
                FreeBlock* rest = reinterpret_cast<FreeBlock*>(reinterpret_cast<uint8_t*>(block) + needed);
                rest->header.size = block->header.size - needed;
                rest->next = block->next;
                *link = rest;
                block->header.size = needed;
            } else {
                *link = block->next;
            }

            freeBytes -= block->header.size;
            return reinterpret_cast<uint8_t*>(block) + kHeaderSize;
        }
        link = &block->next;
    }
    return nullptr;
}

void luaHeap_free(void* ptr) {
    /// @brief Returns a block to the address-ordered free list and merges it with free neighbours.

    if (ptr == nullptr) return;
    FreeBlock* block = reinterpret_cast<FreeBlock*>(static_cast<uint8_t*>(ptr) - kHeaderSize);
    freeBytes += block->header.size;

    // 1. FIND POSITION (address order)
    FreeBlock* prev = nullptr;
    FreeBlock* next = freeList;
    while ((next != nullptr) && (next < block)) {
        prev = next;
        next = next->next;
    }

    // 2. MERGE WITH NEXT
    if ((next != nullptr) && (reinterpret_cast<uint8_t*>(block) + block->header.size == reinterpret_cast<uint8_t*>(next))) {
        block->header.size += next->header.size;
        block->next = next->next;
    } else {
        block->next = next;
    }

    // 3. MERGE WITH PREVIOUS
    if ((prev != nullptr) && (reinterpret_cast<uint8_t*>(prev) + prev->header.size == reinterpret_cast<uint8_t*>(block))) {
        prev->header.size += block->header.size;
        prev->next = block->next;
    } else if (prev != nullptr) {
        prev->next = block;
    } else {
        freeList = block;
    }
}

size_t luaHeap_blockSize(const void* ptr) {
    const BlockHeader* header = reinterpret_cast<const BlockHeader*>(static_cast<const uint8_t*>(ptr) - kHeaderSize);
    return header->size - kHeaderSize;
}

size_t luaHeap_getFree() {
    return freeBytes;
}

size_t luaHeap_getSize() {
    return kLuaHeapSize;
}

#endif
