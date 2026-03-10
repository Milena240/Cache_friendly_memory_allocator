#include "../headers/slab_allocator.hpp"
#include <sys/mman.h>
#include <stdexcept>
#include <thread>
#include <algorithm>


SlabAllocator::SlabPool::SlabPool(std::size_t obj_size, std::size_t count) 
    : object_size(obj_size), free_list(nullptr) 
{
    std::size_t actual_size = std::max(obj_size, sizeof(FreeNode));
    std::size_t total_bytes = actual_size * count;

    raw_memory = ::operator new(total_bytes);
    
    uint8_t* current = static_cast<uint8_t*>(raw_memory);
    for (std::size_t i = 0; i < count; ++i) {
        FreeNode* node = reinterpret_cast<FreeNode*>(current);
        node->next = free_list;
        free_list = node;
        current += actual_size;
    }
}

SlabAllocator::SlabPool::~SlabPool() 
{
    ::operator delete(raw_memory);
}

void* 
SlabAllocator::SlabPool::pop() 
{
    if (!free_list) return nullptr;
    void* ptr = free_list;
    free_list = free_list->next;
    return ptr;
}

void 
SlabAllocator::SlabPool::push(void* ptr) 
{
    if (!ptr) return;
    FreeNode* node = static_cast<FreeNode*>(ptr);
    node->next = free_list;
    free_list = node;
}


SlabAllocator::SlabAllocator(std::size_t min_size, std::size_t max_size, LockType lockType)
    : m_lockType(lockType)
{
    std::size_t current_min = min_size;
    while (current_min <= max_size) {
        std::size_t current_max = std::min(current_min * 2, max_size);
        
        m_size_classes.push_back(SizeClass{
            current_min,
            current_max,
            new SlabPool(current_max, 1000) 
        });

        if (current_max == max_size) break;
        current_min = current_max + 1;
    }
}

SlabAllocator::~SlabAllocator() 
{
    for (auto& sc : m_size_classes) {
        delete sc.pool;
    }
}

void
SlabAllocator::lock()
{
    if (m_lockType == LockType::MUTEX) m_mutex.lock();
    else if (m_lockType == LockType::SPIN_LOCK) {
        while (m_spin_lock.test_and_set(std::memory_order_acquire))
            std::this_thread::yield();
    }
}

void 
SlabAllocator::unlock()
{
    if (m_lockType == LockType::MUTEX) m_mutex.unlock();
    else if (m_lockType == LockType::SPIN_LOCK)
        m_spin_lock.clear(std::memory_order_release);
}

void*
SlabAllocator::alloc(std::size_t size)
{
    for (auto& sc : m_size_classes) {
        if (size <= sc.max_size) {
            lock();
            void* ptr = sc.pool->pop();
            unlock();
            return ptr;
        }
    }
    return nullptr;
}

void
SlabAllocator::free(void* ptr, std::size_t size)
{
    for (auto& sc : m_size_classes) {
        if (size <= sc.max_size) {
            lock();
            sc.pool->push(ptr);
            unlock();
            return;
        }
    }
}
