#include "../headers/allocator.hpp"

#include <sys/mman.h>
#include <stdexcept>
#include <thread>

BumpAllocator::BumpAllocator(std::size_t size, LockType lockType)
    : m_size(size),
      m_lockType(lockType)
{
    if (size == 0) {
        throw std::invalid_argument("Allocator size must be > 0");
    }

    m_start = static_cast<uint8_t*>(
        mmap(nullptr,
             size,
             PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS,
             -1,
             0));

    if (m_start == MAP_FAILED) {
        m_start = nullptr;
        throw std::runtime_error("mmap failed to allocate memory pool");
    }

    m_spin_lock.clear();
}

BumpAllocator::~BumpAllocator()
{
    if (m_start) {
        munmap(m_start, m_size);
    }
}

bool BumpAllocator::is_power_of_two(std::size_t x)
{
    return x && !(x & (x - 1));
}

void BumpAllocator::lock()
{
    if (m_lockType == LockType::MUTEX) {
        m_mutex.lock();
    }
    else if (m_lockType == LockType::SPIN_LOCK) {
        while (m_spin_lock.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
}

void BumpAllocator::unlock()
{
    if (m_lockType == LockType::MUTEX) {
        m_mutex.unlock();
    }
    else if (m_lockType == LockType::SPIN_LOCK) {
        m_spin_lock.clear(std::memory_order_release);
    }
}

void* BumpAllocator::alloc(std::size_t size, std::size_t alignment)
{
    if (size == 0)
        return nullptr;

    if (!is_power_of_two(alignment))
        return nullptr;

    lock();

    uintptr_t current = reinterpret_cast<uintptr_t>(m_start) + m_offset;

    uintptr_t aligned =
        (current + (alignment - 1)) & ~(alignment - 1);

    std::size_t new_offset =
        (aligned - reinterpret_cast<uintptr_t>(m_start)) + size;

    if (new_offset > m_size) {
        unlock();
        return nullptr;
    }

    m_offset = new_offset;

    unlock();

    return reinterpret_cast<void*>(aligned);
}

void BumpAllocator::reset()
{
    lock();
    m_offset = 0;
    unlock();
}

