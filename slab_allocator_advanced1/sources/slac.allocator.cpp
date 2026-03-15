#include "../headers/slab_allocator.hpp"
#include <algorithm>
#include <cstdlib>
#include <new>

// Initialize the static thread-local cache
thread_local SlabAllocator::ThreadCache SlabAllocator::t_cache;

SlabAllocator::SlabPool::SlabPool(std::size_t obj_size, std::size_t count) 
    : free_list(nullptr) 
{
    // SMART ALIGNMENT: 
    // Small objects (<64) get 16-byte alignment to save RAM.
    // Large objects get 64-byte alignment to prevent False Sharing.
    std::size_t alignment = (obj_size < CACHE_LINE) ? 16 : CACHE_LINE;
    
    object_size = (std::max(obj_size, sizeof(FreeNode)) + alignment - 1) & ~(alignment - 1);
    std::size_t total_bytes = object_size * count;

    // Use aligned_alloc to ensure the block starts on a cache line boundary
    raw_memory = std::aligned_alloc(CACHE_LINE, total_bytes);
    if (!raw_memory) throw std::bad_alloc();

    // Link the memory into the free list
    uint8_t* current = static_cast<uint8_t*>(raw_memory);
    for (std::size_t i = 0; i < count; ++i) {
        FreeNode* node = reinterpret_cast<FreeNode*>(current);
        node->next = free_list.load(std::memory_order_relaxed);
        free_list.store(node, std::memory_order_relaxed);
        current += object_size;
    }
}

SlabAllocator::SlabPool::~SlabPool() {
    std::free(raw_memory);
}

void SlabAllocator::SlabPool::push(void* ptr) {
    FreeNode* node = static_cast<FreeNode*>(ptr);
    FreeNode* old_head = free_list.load(std::memory_order_relaxed);
    do {
        node->next = old_head;
    } while (!free_list.compare_exchange_weak(old_head, node, 
                                            std::memory_order_release, 
                                            std::memory_order_relaxed));
}

void* SlabAllocator::SlabPool::pop() {
    FreeNode* old_head = free_list.load(std::memory_order_acquire);
    while (old_head && !free_list.compare_exchange_weak(old_head, old_head->next, 
                                                       std::memory_order_release, 
                                                       std::memory_order_relaxed));
    return old_head;
}

SlabAllocator::SlabAllocator(std::size_t min_size, std::size_t max_size) {
    std::size_t current_size = min_size;
    while (current_size <= max_size) {
        m_size_classes.push_back({
            current_size, 
            new SlabPool(current_size, 1024) // Pre-allocate 1024 slots per size
        });
        
        if (current_size >= max_size) break;
        current_size *= 2; // Power-of-two size classes (16, 32, 64, 128...)
    }
}

SlabAllocator::~SlabAllocator() {
    for (auto& sc : m_size_classes) delete sc.pool;
}

int SlabAllocator::get_index(std::size_t size) const {
    for (int i = 0; i < (int)m_size_classes.size(); ++i) {
        if (size <= m_size_classes[i].max_size) return i;
    }
    return -1;
}

void* SlabAllocator::alloc(std::size_t size) {
    int idx = get_index(size);
    if (idx == -1) return nullptr;

    // 1. Path 1: Thread-Local Cache (No Locks, No Atomics)
    if (t_cache.counts[idx] > 0) {
        FreeNode* node = t_cache.heads[idx];
        t_cache.heads[idx] = node->next;
        t_cache.counts[idx]--;
        return node;
    }

    // 2. Path 2: Global Pool Fallback (Atomic Lock-Free)
    return m_size_classes[idx].pool->pop();
}

void SlabAllocator::free(void* ptr, std::size_t size) {
    if (!ptr) return;
    int idx = get_index(size);
    if (idx == -1) return;

    // 1. Path 1: Put back in Thread-Local Cache to keep it "Warm"
    if (t_cache.counts[idx] < ThreadCache::MAX_LOCAL_BLOCKS) {
        FreeNode* node = static_cast<FreeNode*>(ptr);
        node->next = t_cache.heads[idx];
        t_cache.heads[idx] = node;
        t_cache.counts[idx]++;
    } else {
        // 2. Path 2: Cache full, return to Global Pool
        m_size_classes[idx].pool->push(ptr);
    }
}
