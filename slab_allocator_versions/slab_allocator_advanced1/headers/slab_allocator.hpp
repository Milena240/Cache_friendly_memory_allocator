#ifndef SLAB_ALLOCATOR_HPP
#define SLAB_ALLOCATOR_HPP

#include <atomic>
#include <vector>
#include <cstddef>

// Cache line size for most modern CPUs
constexpr std::size_t CACHE_LINE = 64;

// Aligned to prevent metadata from causing false sharing
struct alignas(16) FreeNode {
    FreeNode* next;
};

class SlabAllocator {
public:
    // Internal Pool structure managing a specific chunk of memory
    struct SlabPool {
        std::size_t object_size;
        std::atomic<FreeNode*> free_list;
        void* raw_memory;

        SlabPool(std::size_t obj_size, std::size_t count);
        ~SlabPool();

        // Atomic Lock-Free operations
        void push(void* ptr);
        void* pop();
    };

    struct SizeClass {
        std::size_t max_size;
        SlabPool* pool;
    };

    SlabAllocator(std::size_t min_size, std::size_t max_size);
    ~SlabAllocator();

    void* alloc(std::size_t size);
    void  free(void* ptr, std::size_t size);

private:
    std::vector<SizeClass> m_size_classes;
    
    // Thread-Local Cache: The "Fast Lane"
    struct ThreadCache {
        static constexpr std::size_t MAX_LOCAL_BLOCKS = 16;
        // Supports up to 32 size classes (plenty for min/max range)
        FreeNode* heads[32] = {nullptr}; 
        std::size_t counts[32] = {0};
    };

    static thread_local ThreadCache t_cache;

    // Helper to find the correct size class index
    int get_index(std::size_t size) const;
};

#endif
