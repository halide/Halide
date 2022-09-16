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

halide_tls_key_t my_key = nullptr;

struct MyRuntimeStruct {
    uint64_t signature = 0xdeadbeeff00dcafe;
};
thread_local MyRuntimeStruct my_runtime;

extern "C" {
extern pthread_t pthread_self();
extern int pthread_threadid_np(pthread_t thread, uint64_t *thread_id);
}

uint64_t _gettid() {
    uint64_t id = 0xdeadbeef;
    (void) pthread_threadid_np(pthread_self(), &id);
    return id;
}

extern "C" int sleeper(int loc, int x, int y, int z, int v) {
    // std::cout << "sleeper thread is: " + std::to_string(_gettid()) + "\n";
    void *my_tls_value = halide_get_tls(my_key);
    if (my_tls_value != &my_runtime) {
        std::cerr << std::to_string(_gettid()) + ": Expected TLS value to be " + std::to_string((uintptr_t)&my_runtime) + " but got " + std::to_string((uintptr_t)my_tls_value) + "\n";
        abort();
        exit(1);
    }
    assert(my_runtime.signature == 0xdeadbeeff00dcafe);
    return v;
}

std::mutex error_msg_mutex;
std::string error_msg;

void my_error_handler(void *user_context, const char *msg) {
    std::lock_guard<std::mutex> lock(error_msg_mutex);

    assert(user_context == nullptr);
    assert(error_msg.empty());
    std::cerr << "my_error_handler: " << error_msg << "\n";
    error_msg = msg;
}

void test_alloc_dealloc_all() {
    std::vector<halide_tls_key_t> keys;
    for (;;) {
        halide_tls_key_t k = halide_allocate_tls_key();
        if (k == nullptr) break;
        keys.push_back(k);
    }
    std::cout << "Allocated: " << keys.size() << " halide_tls_key_t(s).\n";
    while (!keys.empty()) {
        int r = halide_free_tls_key(keys.back());
        if (r != 0) {
            std::cerr << "Failed to free a key.\n";
            exit(1);
        }
        keys.pop_back();
    }
}

}

int main(int argc, char **argv) {
    test_alloc_dealloc_all();

    std::cout << "main thread is: " + std::to_string(_gettid()) + "\n";

    my_key = halide_allocate_tls_key();
    assert(my_key != nullptr);

    halide_set_error_handler(my_error_handler);
    halide_set_num_threads(1);

    std::vector<std::thread> threads;
    for (int i = 0; i < 2; i++) {
        threads.emplace_back([&]() {
            std::cout << "runner thread is: " + std::to_string(_gettid()) + " &my_runtime -> " + std::to_string((uintptr_t)&my_runtime) + "\n";

            int result = halide_set_tls(my_key, &my_runtime);
            assert(result == 0);

            constexpr int edge = 16;
            Halide::Runtime::Buffer<int, 3> out(edge, edge, edge);
            result = async_parallel(out);
            assert(result == 0);

            std::cout << "DONE with " + std::to_string(i) + "\n";
        });
    }
    for (auto &t : threads) {
        t.join();
    }

    halide_free_tls_key(my_key);
    my_key = nullptr;

    std::cout << "Success!\n";
    return 0;
}
