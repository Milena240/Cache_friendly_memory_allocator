#include "../headers/allocator.hpp"
#include <cassert>
#include <iostream>

void 
test_alignment() {
    BumpAllocator alloc(1024, LockType::NONE);
    void* p1 = alloc.alloc(1, 64);
    assert(((uintptr_t)p1 % 64) == 0);
    std::cout << "[PASS] 64-byte alignment confirmed." << std::endl;
}

void 
test_thread_safety_simple() {
    BumpAllocator alloc(1024, LockType::SPIN_LOCK);
    void* p1 = alloc.alloc(10);
    assert(p1 != nullptr);
    std::cout << "[PASS] Spinlock allocation works." << std::endl;
}

void 
test_thread_safety_simple2() {
    BumpAllocator alloc(1024, LockType::MUTEX);
    void* p1 = alloc.alloc(10);
    assert(p1 != nullptr);
    std::cout << "[PASS] Mutex allocation works." << std::endl;
}

int 
main() {
    test_alignment();
    test_thread_safety_simple();
    test_thread_safety_simple2();
    std::cout << "ALL UNIT TESTS PASSED!" << std::endl;
    return 0;
}
