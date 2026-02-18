#include <iostream>
#include <thread>
#include <chrono>

struct NoFalseSharing {
    int a;
    char padding[64]; 
    int b;
};

NoFalseSharing data;

void thread1() {
    for (long i = 0; i < 100000000L; i++) {
        data.a++;
    }
}

void thread2() {
    for (long i = 0; i < 100000000L; i++) {
        data.b++;
    }
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();

    std::thread t1(thread1);
    std::thread t2(thread2);

    t1.join();
    t2.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "Cache friendly time: " << elapsed.count() << " seconds\n";
    return 0;
}

