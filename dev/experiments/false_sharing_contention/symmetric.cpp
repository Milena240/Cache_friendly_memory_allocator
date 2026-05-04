#include <iostream>
#include <thread>
#include <sched.h>

struct Data {
    int a = 0;
    int c = 0;
};

void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
}

int main() {
    Data data;
    const long long iterations = 1000000000;

    std::thread t1([&]() {
        pin_to_core(0);
        for (long long i = 0; i < iterations; ++i) {
            data.a++;
        }
    });

    std::thread t2([&]() {
        pin_to_core(1);
        for (long long i = 0; i < iterations; ++i) {
            data.c++;
        }
    });

    t1.join();
    t2.join();
    return 0;
}
