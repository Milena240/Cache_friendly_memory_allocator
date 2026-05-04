#include "../headers/slab_allocator.hpp"

#include <sys/mman.h>
#include <stdexcept>
#include <thread>

SlabAllocator::SlabAllocator(std::size_t objectSize,
                             std::size_t objectCount,
                             LockType lockType)
    : m_objectSize(objectSize < sizeof(FreeNode*)
                       ? sizeof(FreeNode*)
                       : objectSize),
      m_capacity(objectCount),
      m_lockType(lockType)
{
    std::size_t totalSize = m_objectSize * m_capacity;

    m_memory = static_cast<uint8_t*>(
        mmap(nullptr,
             totalSize,
             PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS,
             -1,
             0));

    if (m_memory == MAP_FAILED) {
        m_memory = nullptr;
        throw std::runtime_error("mmap failed in SlabAllocator");
    }

    m_freeList = nullptr;
    for (std::size_t i = 0; i < m_capacity; ++i) {
        uint8_t* addr = m_memory + i * m_objectSize;
        FreeNode* node = reinterpret_cast<FreeNode*>(addr);
        node->next = m_freeList;
        m_freeList = node;
    }

    m_spin_lock.clear();
}

SlabAllocator::~SlabAllocator()
{
    if (m_memory) {
        munmap(m_memory, m_objectSize * m_capacity);
    }
}

void SlabAllocator::lock()
{
    if (m_lockType == LockType::MUTEX)
        m_mutex.lock();
    else if (m_lockType == LockType::SPIN_LOCK)
        while (m_spin_lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();
}

void SlabAllocator::unlock()
{
    if (m_lockType == LockType::MUTEX)
        m_mutex.unlock();
    else if (m_lockType == LockType::SPIN_LOCK)
        m_spin_lock.clear(std::memory_order_release);
}

void* SlabAllocator::alloc()
{
    lock();
    if (!m_freeList) {
        unlock();
        return nullptr; 
    }

    FreeNode* node = m_freeList;
    m_freeList = node->next;

    unlock();
    return node;
}

void SlabAllocator::free(void* ptr)
{
    if (!ptr)
        return;

    lock();

    FreeNode* node = reinterpret_cast<FreeNode*>(ptr);
    node->next = m_freeList;
    m_freeList = node;

    unlock();
}
