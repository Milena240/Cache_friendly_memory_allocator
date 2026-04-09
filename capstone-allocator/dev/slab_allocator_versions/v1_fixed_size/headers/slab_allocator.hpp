#ifndef SLAB_ALLOCATOR_HPP
#define SLAB_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <atomic>

enum class LockType { NONE, MUTEX, SPIN_LOCK };

class SlabAllocator
{
public:
    SlabAllocator(std::size_t objectSize,
                  std::size_t objectCount,
                  LockType lockType = LockType::NONE);

    ~SlabAllocator();

    SlabAllocator(const SlabAllocator&) = delete;
    SlabAllocator& operator=(const SlabAllocator&) = delete;

    void* alloc();
    void  free(void* ptr);

    std::size_t capacity() const noexcept { return m_capacity; }
    std::size_t object_size() const noexcept { return m_objectSize; }

private:
    struct FreeNode {
        FreeNode* next;
    };

    void lock();
    void unlock();

private:
    uint8_t* m_memory = nullptr;

    FreeNode* m_freeList = nullptr;

    std::size_t m_objectSize;
    std::size_t m_capacity;

    LockType m_lockType;

    std::mutex m_mutex;
    std::atomic_flag m_spin_lock = ATOMIC_FLAG_INIT;
};

#endif
