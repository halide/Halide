#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <atomic>
#include <chrono>
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "thread_pool_keep_awake.h"

using namespace Halide::Runtime;

static_assert(!std::is_copy_constructible_v<ThreadPoolKeepAwake>);
static_assert(!std::is_copy_assignable_v<ThreadPoolKeepAwake>);
static_assert(!std::is_move_constructible_v<ThreadPoolKeepAwake>);

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: Check failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

namespace {

std::atomic<int> errors_reported{0};
std::string last_error;

void record_error(void *user_context, const char *msg) {
    errors_reported++;
    last_error = msg;
}

// Only meaningful when no other thread is changing the count.
int keep_awake_count() {
    int c = halide_thread_pool_keep_awake(true);
    CHECK(c >= 1);
    CHECK(halide_thread_pool_keep_awake(false) == c - 1);
    return c - 1;
}

// Busy-wait for about the given number of microseconds, to leave a gap
// between parallel loops like a real workload's serial work would.
void serial_gap(int us) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::microseconds(us)) {
    }
}

void run_pipeline(int offset, int w = 32, int h = 8) {
    Buffer<int, 2> out(w, h);
    CHECK(thread_pool_keep_awake(offset, out) == 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            CHECK(out(x, y) == 2 * x * y + 2 * offset + 1);
        }
    }
}

struct ParForClosure {
    int *hits;
    int inner;
    int fail_at;
};

int inner_task(void *user_context, int idx, uint8_t *closure) {
    ((int *)closure)[idx]++;
    return 0;
}

int outer_task(void *user_context, int idx, uint8_t *closure) {
    ParForClosure *c = (ParForClosure *)closure;
    if (idx == c->fail_at) {
        return halide_error_code_generic_error;
    }
    c->hits[idx]++;
    if (c->inner > 0) {
        // Nested parallelism.
        std::vector<int> inner_hits(c->inner, 0);
        int ret = halide_do_par_for(user_context, inner_task, 0, c->inner, (uint8_t *)inner_hits.data());
        if (ret != 0) {
            return ret;
        }
        for (int h : inner_hits) {
            if (h != 1) {
                return halide_error_code_generic_error;
            }
        }
    }
    return 0;
}

void run_par_for(int n, int inner = 0) {
    std::vector<int> hits(n, 0);
    ParForClosure c{hits.data(), inner, -1};
    CHECK(halide_do_par_for(nullptr, outer_task, 0, n, (uint8_t *)&c) == 0);
    for (int h : hits) {
        CHECK(h == 1);
    }

    // Errors must still propagate.
    if (n > 1) {
        c.fail_at = n / 2;
        CHECK(halide_do_par_for(nullptr, outer_task, 0, n, (uint8_t *)&c) == halide_error_code_generic_error);
    }
}

constexpr int pc_size = 64;

struct ProducerConsumerState {
    halide_semaphore_t sem;
    int data[pc_size];
    int consumed[pc_size];
};

int producer_task(void *user_context, int min, int extent, uint8_t *closure, void *task_parent) {
    ProducerConsumerState *s = (ProducerConsumerState *)closure;
    for (int i = min; i < min + extent; i++) {
        if (i % 16 == 0) {
            // Some nested parallelism inside a task.
            std::vector<int> inner_hits(8, 0);
            if (halide_do_par_for(user_context, inner_task, 0, 8, (uint8_t *)inner_hits.data()) != 0) {
                return halide_error_code_generic_error;
            }
        }
        s->data[i] = 3 * i + 1;
        halide_semaphore_release(&s->sem, 1);
    }
    return 0;
}

int consumer_task(void *user_context, int min, int extent, uint8_t *closure, void *task_parent) {
    ProducerConsumerState *s = (ProducerConsumerState *)closure;
    for (int i = min; i < min + extent; i++) {
        s->consumed[i] = s->data[i];
    }
    return 0;
}

void run_producer_consumer() {
    ProducerConsumerState s;
    memset(&s, 0, sizeof(s));
    halide_semaphore_init(&s.sem, 0);

    halide_semaphore_acquire_t acquire = {&s.sem, 1};
    halide_parallel_task_t tasks[2];
    memset(tasks, 0, sizeof(tasks));
    // The consumer goes first, so that threads find it blocked on its
    // semaphore before the producer gets going.
    tasks[0].fn = consumer_task;
    tasks[0].closure = (uint8_t *)&s;
    tasks[0].name = "consumer";
    tasks[0].semaphores = &acquire;
    tasks[0].num_semaphores = 1;
    tasks[0].min = 0;
    tasks[0].extent = pc_size;
    tasks[0].serial = true;
    tasks[1].fn = producer_task;
    tasks[1].closure = (uint8_t *)&s;
    tasks[1].name = "producer";
    tasks[1].min = 0;
    tasks[1].extent = pc_size;
    tasks[1].serial = true;

    CHECK(halide_do_parallel_tasks(nullptr, 2, tasks, nullptr) == 0);
    for (int i = 0; i < pc_size; i++) {
        CHECK(s.consumed[i] == 3 * i + 1);
    }
}

void run_everything(int i) {
    run_pipeline(i);
    run_par_for(1 + i % 13);
    if (i % 4 == 0) {
        run_par_for(6, 3);
        run_producer_consumer();
    }
}

void test_counting() {
    CHECK(keep_awake_count() == 0);
    CHECK(halide_thread_pool_keep_awake(true) == 1);
    CHECK(halide_thread_pool_keep_awake(true) == 2);
    CHECK(halide_thread_pool_keep_awake(false) == 1);
    CHECK(halide_thread_pool_keep_awake(false) == 0);

    {
        ThreadPoolKeepAwake a;
        CHECK(keep_awake_count() == 1);
        {
            ThreadPoolKeepAwake b;
            CHECK(keep_awake_count() == 2);
            {
                ThreadPoolKeepAwake c;
                CHECK(keep_awake_count() == 3);
            }
            CHECK(keep_awake_count() == 2);
        }
        CHECK(keep_awake_count() == 1);
    }
    CHECK(keep_awake_count() == 0);
}

void test_unbalanced_release() {
    halide_error_handler_t old_handler = halide_set_error_handler(record_error);

    errors_reported = 0;
    CHECK(halide_thread_pool_keep_awake(false) == halide_error_code_generic_error);
    CHECK(errors_reported == 1);
    CHECK(last_error.find("halide_thread_pool_keep_awake") != std::string::npos);
    // The count stays at zero, and works as normal afterwards.
    CHECK(halide_thread_pool_keep_awake(true) == 1);
    CHECK(halide_thread_pool_keep_awake(false) == 0);
    CHECK(halide_thread_pool_keep_awake(false) == halide_error_code_generic_error);
    CHECK(errors_reported == 2);
    CHECK(keep_awake_count() == 0);
    run_everything(0);

    halide_set_error_handler(old_handler);
}

void test_many_small_loops() {
    // Alternate between holding and not holding the count, with and
    // without serial gaps between the loops, including one long enough
    // for idle threads to give up and go to sleep anyway.
    std::mt19937 rng(0);
    for (int phase = 0; phase < 4; phase++) {
        if (phase % 2) {
            ThreadPoolKeepAwake keep_awake;
            for (int i = 0; i < 300; i++) {
                run_everything(i);
                serial_gap(phase == 1 ? 0 : (int)(rng() % 200));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            run_everything(1);
        } else {
            for (int i = 0; i < 300; i++) {
                run_everything(i);
                serial_gap(phase == 0 ? 0 : (int)(rng() % 200));
            }
        }
    }
    CHECK(keep_awake_count() == 0);
}

void test_small_then_big() {
    // While the count is held, workers that a small loop doesn't need stay on
    // the A team rather than being demoted to the B team, and acquiring the
    // count wakes workers that a small loop already demoted. Mix small and big
    // loops around acquiring and releasing the count; results must be correct
    // throughout, and threads demoted before or after must still pick up work.
    halide_set_num_threads(0);
    const int threads = halide_get_num_threads();
    for (int round = 0; round < 20; round++) {
        // Without the count, a small loop after a big one demotes workers.
        run_par_for(2 * threads);
        run_par_for(1 + round % 2);
        serial_gap(round % 3 == 0 ? 100 : 0);
        {
            ThreadPoolKeepAwake keep_awake;
            for (int i = 0; i < 50; i++) {
                run_par_for(1 + i % 2);
                serial_gap(i % 5 == 0 ? 100 : 0);
                run_par_for(2 * threads);
                if (i % 10 == 0) {
                    run_par_for(4, 3);
                    run_producer_consumer();
                }
            }
            run_par_for(1);
        }
        // After releasing, idle workers on the A team demote again as usual.
        run_par_for(1);
        run_par_for(2 * threads);
    }
    CHECK(keep_awake_count() == 0);
}

void test_set_num_threads() {
    halide_set_num_threads(0);
    const int default_threads = halide_get_num_threads();
    ThreadPoolKeepAwake keep_awake;
    for (int n : {1, 2, 4, 1, 8, 3, 16, 2, 0, 5}) {
        halide_set_num_threads(n);
        CHECK(halide_get_num_threads() == (n == 0 ? default_threads : n));
        for (int i = 0; i < 40; i++) {
            run_everything(i);
            serial_gap(i % 3 == 0 ? 50 : 0);
        }
    }
    halide_set_num_threads(default_threads);
    CHECK(keep_awake_count() == 1);
}

void test_shutdown() {
    {
        ThreadPoolKeepAwake keep_awake;
        // Shut down while idle threads are kept awake.
        run_everything(0);
        halide_shutdown_thread_pool();
        CHECK(keep_awake_count() == 1);
        // The pool restarts, with its count still held.
        run_everything(4);
        halide_shutdown_thread_pool();
        CHECK(keep_awake_count() == 1);
        // A shutdown with no pool running is a no-op.
        halide_shutdown_thread_pool();
        run_everything(8);
    }
    CHECK(keep_awake_count() == 0);
    halide_shutdown_thread_pool();
    run_everything(12);

    // Acquire the first reference while the pool is shut down.
    halide_shutdown_thread_pool();
    {
        ThreadPoolKeepAwake keep_awake;
        run_everything(16);
    }
    halide_shutdown_thread_pool();
    CHECK(keep_awake_count() == 0);
}

void test_concurrent() {
    // Several threads use the pool and hold or release references
    // concurrently, while other threads toggle the count and the number of
    // threads.
    std::atomic<bool> stop{false};
    halide_set_num_threads(0);
    const int default_threads = halide_get_num_threads();

    std::thread toggler([&]() {
        while (!stop) {
            ThreadPoolKeepAwake keep_awake;
            serial_gap(20);
        }
    });

    std::thread resizer([&]() {
        std::mt19937 rng(1);
        while (!stop) {
            halide_set_num_threads(1 + rng() % 8);
            serial_gap(100);
        }
    });

    std::vector<std::thread> users;
    for (int t = 0; t < 3; t++) {
        users.emplace_back([t]() {
            for (int i = 0; i < 100; i++) {
                if ((i / 10 + t) % 2) {
                    ThreadPoolKeepAwake keep_awake;
                    run_everything(i);
                } else {
                    run_everything(i);
                }
            }
        });
    }
    for (auto &u : users) {
        u.join();
    }
    stop = true;
    toggler.join();
    resizer.join();
    halide_set_num_threads(default_threads);
    CHECK(keep_awake_count() == 0);
}

}  // namespace

int main(int argc, char **argv) {
    test_counting();
    test_unbalanced_release();
    test_many_small_loops();
    test_small_then_big();
    test_set_num_threads();
    test_shutdown();
    test_concurrent();
    CHECK(keep_awake_count() == 0);

    printf("Success!\n");
    return 0;
}
