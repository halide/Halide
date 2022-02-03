#include <assert.h>
#include <atomic>
#include <iostream>
#include <mutex>
#include <random>
#include <stdio.h>
#include <string>
#include <thread>

#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include "async_parallel.h"

std::atomic<uint64_t> last_update{0};

uint64_t sleeps;

struct last_call {
    int loc, x, y, z;
    last_call *next;
    bool inited;

    bool operator<(const last_call &rhs) const {
        if (loc < rhs.loc) {
            return true;
        } else if (loc >= rhs.loc) {
            return false;
        }
        if (x < rhs.x) {
            return true;
        } else if (x >= rhs.x) {
            return false;
        }
        if (y < rhs.y) {
            return true;
        } else if (y >= rhs.y) {
            return false;
        }
        if (z < rhs.z) {
            return true;
        } else if (z >= rhs.z) {
            return false;
        }
        return this < &rhs;
    }
};

std::mutex watchdog_state_mutex;
// Protects these variables
bool watchdog_done = false;
last_call *all_thread_lasts = nullptr;
// End mutexed state

thread_local last_call thread_last = {};

extern "C" int sleeper(int loc, int x, int y, int z, int v) {
    last_update++;

    thread_last.loc = loc;
    thread_last.x = x;
    thread_last.y = y;
    thread_last.z = z;

    if (!thread_last.inited) {
        std::lock_guard<std::mutex> lock(watchdog_state_mutex);

        thread_last.next = all_thread_lasts;
        all_thread_lasts = &thread_last;
        thread_last.inited = true;
    }

    if ((sleeps & ((1UL << (7 - loc)) | (8UL << (x + 1)))) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return v;
}

void watchdog() {
    int count = 0;
    while (true) {
        uint64_t prev = last_update;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (prev == last_update) {
            std::lock_guard<std::mutex> lock(watchdog_state_mutex);

            if (watchdog_done) {
                return;
            }
            if (count < 3) {
                count++;
                continue;
            }

            int thread_count = 0;
            last_call *best = all_thread_lasts;
            last_call *lc = all_thread_lasts;
            while (lc != nullptr) {
                if (*lc < *best) {
                    best = lc;
                }
                thread_count++;
                lc = lc->next;
            }
            fflush(stdout);
            fflush(stderr);
            if (best == nullptr) {
                std::cout << "Hung before any sleeps on any thread.\n";
            } else {
                std::cout << "Hung at loc " << best->loc << "(" << best->x << ", " << best->y << ", " << best->z << ") sleeps: " << sleeps << " threads: " << thread_count << "\n";
            }
            _Exit(1);
        } else {
            count = 0;
        }
    }
}

int main(int argc, char **argv) {
    uint64_t start;
    uint64_t count = 1;
    if (argc > 1) {
        start = std::stoull(argv[1]);
    } else {
        std::mt19937 rng(time(nullptr));
        std::uniform_int_distribution<> dis(0, 65535);
        start = dis(rng);
    }

    if (argc > 2) {
        count = std::stoull(argv[1]);
    }

    std::thread watcher(watchdog);

    while (count-- > 0) {
        sleeps = start++;
        Halide::Runtime::Buffer<int, 3> out(8, 8, 8);

        async_parallel(out);
    }

    {
        std::lock_guard<std::mutex> lock(watchdog_state_mutex);
        watchdog_done = true;
    }

    watcher.join();

    std::cout << "Success!\n";
    return 0;
}
