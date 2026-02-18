#ifndef ALLOCATOR_HPP
#define ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <atomic>

enum class LockType { NONE, MUTEX, SPIN_LOCK };

class BumpAllocator
{
public:
    explicit BumpAllocator(std::size_t size,
                           LockType lockType = LockType::NONE);
    ~BumpAllocator();

    // Non-copyable
    BumpAllocator(const BumpAllocator&) = delete;
    BumpAllocator& operator=(const BumpAllocator&) = delete;

    void* alloc(std::size_t size, std::size_t alignment = 16);
    void  reset();

    std::size_t capacity() const noexcept { return m_size; }
    std::size_t used() const noexcept { return m_offset; }

private:
    void lock();
    void unlock();

    static bool is_power_of_two(std::size_t x);

private:
    uint8_t* m_start = nullptr;
    std::size_t m_size = 0;

    alignas(64) std::size_t m_offset = 0;

    LockType m_lockType;

    std::mutex m_mutex;
    std::atomic_flag m_spin_lock = ATOMIC_FLAG_INIT;
};

#endif // ALLOCATOR_HPP

