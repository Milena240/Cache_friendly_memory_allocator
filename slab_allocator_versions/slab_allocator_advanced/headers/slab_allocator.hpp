#ifndef SLAB_ALLOCATOR_HPP
#define SLAB_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <vector>

enum class LockType { NONE, MUTEX, SPIN_LOCK };

class SlabAllocator 
{
public:
    SlabAllocator(std::size_t min_size, std::size_t max_size, LockType lockType = LockType::NONE);
    ~SlabAllocator();

    SlabAllocator(const SlabAllocator&) = delete;
    SlabAllocator& operator=(const SlabAllocator&) = delete;

    void* alloc(std::size_t size);
    void free(void* ptr, std::size_t size);

private:
    struct FreeNode {
        FreeNode* next;
    };

    struct SlabPool {
        std::size_t object_size;
        void* raw_memory;
        FreeNode* free_list;

        SlabPool(std::size_t obj_size, std::size_t count);
        ~SlabPool();
        void* pop();
        void push(void* ptr);
    };

    struct SizeClass {
        std::size_t min_size;
        std::size_t max_size;
        SlabPool* pool; 
    };

    void lock();
    void unlock();

    std::vector<SizeClass> m_size_classes;
    LockType m_lockType;
    std::mutex m_mutex;
    std::atomic_flag m_spin_lock = ATOMIC_FLAG_INIT;
};

#endif
