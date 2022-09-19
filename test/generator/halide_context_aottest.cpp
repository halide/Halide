#include <cassert>
#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "async_parallel.h"

namespace {

halide_context_key_t my_key = nullptr;

// extern "C" {
// extern pthread_t pthread_self();
// extern int pthread_threadid_np(pthread_t thread, uint64_t *thread_id);
// }

uint64_t _gettid() {
    uint64_t id = 0xdeadbeef;
    //(void) pthread_threadid_np(pthread_self(), &id);
    return id;
}

extern "C" int sleeper(void *user_context, int loc, int x, int y, int z, int v) {
    void *my_tls_value = halide_context_get_value(my_key);
    if (my_tls_value != user_context) {
        std::cerr << std::to_string(_gettid()) + ": Expected my_tls_value to be " + std::to_string((intptr_t)user_context) + " but got " + std::to_string((intptr_t)my_tls_value) + "\n";
        abort();
    }
    return v;
}

void test_alloc_dealloc_all() {
    std::vector<halide_context_key_t> keys;
    for (;;) {
        halide_context_key_t k = halide_context_allocate_key();
        if (k == nullptr) break;
        keys.push_back(k);
    }
    std::cout << "Allocated: " << keys.size() << " halide_context_key_t(s).\n";
    while (!keys.empty()) {
        int r = halide_context_free_key(keys.back());
        if (r != 0) {
            std::cerr << "Failed to free a key.\n";
            exit(1);
        }
        keys.pop_back();
    }
}

void test_threads(int num_halide_threads, int num_cpp_threads) {
    std::cout << "Testing with num_halide_threads=" << num_halide_threads << " called from " << num_cpp_threads << " C++ threads\n";

    halide_set_num_threads(num_halide_threads);

    std::vector<std::thread> threads;
    for (int i = 0; i < num_cpp_threads; i++) {
        threads.emplace_back([&]() {
            intptr_t ucon = i + 1;

            // std::cout << "runner thread is: " + std::to_string(_gettid()) + "ucon -> " + std::to_string(ucon) + "\n";

            int result = halide_context_set_value(my_key, (void *)ucon);
            assert(result == 0);

            constexpr int edge = 16;
            Halide::Runtime::Buffer<int, 3> out(edge, edge, edge);
            result = async_parallel((void *)ucon, out);
            assert(result == 0);

            // std::cout << "DONE with " + std::to_string(i) + "\n";
        });
    }
    for (auto &t : threads) {
        t.join();
    }

}

}

int main(int argc, char **argv) {
    test_alloc_dealloc_all();

    std::cout << "main thread is: " + std::to_string(_gettid()) + "\n";

    my_key = halide_context_allocate_key();
    assert(my_key != nullptr);

    constexpr int max_halide_threads = 16;
    constexpr int max_cpp_threads = 8;
    for (int num_cpp_threads = 1; num_cpp_threads <= max_cpp_threads; num_cpp_threads++) {
        for (int num_halide_threads = max_cpp_threads/2; num_halide_threads <= max_cpp_threads*2; num_halide_threads *= 2) {
            test_threads(num_halide_threads, num_cpp_threads);
        }
    }

    halide_context_free_key(my_key);
    my_key = nullptr;

    std::cout << "Success!\n";
    return 0;
}
