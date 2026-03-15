#include <iostream>
#include <thread>
#include <chrono>
#include <sched.h>
#include <vector>

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

double run_experiment(bool symmetric, long long iterations) {
    Data data;
    auto start = std::chrono::high_resolution_clock::now();

    std::thread t1([&]() {
        pin_to_core(0);
        for (long long i = 0; i < iterations; ++i) {
            data.a++;
        }
    });

    std::thread t2([&]() {
        pin_to_core(1);
        for (long long i = 0; i < iterations; ++i) {
            if (symmetric) {
                data.c++; // Experiment 2: Every time
            } else {
                if (i % 100 == 0) data.c++; // Experiment 1: Every 100th
            }
        }
    });

    t1.join();
    t2.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    return diff.count();
}

int main() {
    const long long iterations = 500000000; // 500 Million

    std::cout << "Running Experiment 1 (Asymmetric - Thread B @ 1/100th speed)..." << std::endl;
    double time1 = run_experiment(false, iterations);
    std::cout << "Time: " << time1 << " seconds\n" << std::endl;

    std::cout << "Running Experiment 2 (Symmetric - Both threads @ full speed)..." << std::endl;
    double time2 = run_experiment(true, iterations);
    std::cout << "Time: " << time2 << " seconds\n" << std::endl;

    std::cout << "--- Final Results ---" << std::endl;
    std::cout << "Symmetric is " << (time2 / time1) << "x slower than Asymmetric." << std::endl;
    
    return 0;
}
