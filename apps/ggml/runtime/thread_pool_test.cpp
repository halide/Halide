// Stress test of the app's copy of Halide's thread pool (README.md), through a
// real Halide pipeline (the q4_0 x q8_0 mul_mat kernel) and direct calls to the
// thread-pool entry points: nested parallel loops, do_parallel_tasks with
// semaphores, error propagation, the keep-awake count held and not with gaps
// between loops, thread-count changes, shutdown and restart, and concurrent
// callers, and the lock-free fast path for parallel loops, including with
// helpers that are late or never come, with more threads than cores, and with
// fewer threads than workers. It
// checks results only, never timings.
#include "HalideRuntime.h"
#include "q4_0_q8_0_mul_mat_checked.h"
#include "thread_pool.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

using ggml_halide::ThreadPoolKeepAwake;

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

void record_error(void *, const char *msg) {
    errors_reported++;
    last_error = msg;
}

// Only meaningful when no other thread is changing the count.
int keep_awake_count() {
    int c = ggml_halide_thread_pool_keep_awake(true);
    CHECK(c >= 1);
    CHECK(ggml_halide_thread_pool_keep_awake(false) == c - 1);
    return c - 1;
}

// Busy-wait for about the given number of microseconds, to leave a gap
// between parallel loops like a real workload's serial work would.
void serial_gap(int us) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::microseconds(us)) {
    }
}

// The q4_0 x q8_0 mul_mat kernel (parallel over tasks of 32 rows, or of 32 x
// 32 outputs for M > 1), on blocks with unit scales and random codes, so the
// result is an exact integer.
struct MulMat {
    static constexpr int K = 256, B = K / 32;
    struct Q4 {
        uint16_t d;
        uint8_t qs[16];
    };
    struct Q8 {
        uint16_t d;
        int8_t qs[32];
    };
    static_assert(sizeof(Q4) == 18 && sizeof(Q8) == 34);
    int N, M;
    std::vector<Q4> w;
    std::vector<Q8> a;
    std::vector<float> expected;

    MulMat(int N, int M, int seed)
        : N(N), M(M), w(B * N), a(B * M), expected(N * M) {
        std::mt19937 rng(seed);
        for (Q4 &q : w) {
            q.d = 0x3c00;  // 1.0 in fp16
            for (uint8_t &c : q.qs)
                c = rng();
        }
        for (Q8 &q : a) {
            q.d = 0x3c00;
            for (int8_t &c : q.qs)
                c = (int8_t)(rng() % 255 - 127);
        }
        for (int m = 0; m < M; m++) {
            for (int n = 0; n < N; n++) {
                int sum = 0;
                for (int b = 0; b < B; b++) {
                    const Q4 &x = w[n * B + b];
                    const Q8 &y = a[m * B + b];
                    for (int j = 0; j < 16; j++) {
                        sum += ((x.qs[j] & 15) - 8) * y.qs[j] + ((x.qs[j] >> 4) - 8) * y.qs[j + 16];
                    }
                }
                expected[m * N + n] = (float)sum;
            }
        }
    }

    void run() {
        const halide_filter_metadata_t *md = q4_0_q8_0_mul_mat_checked_metadata();
        std::vector<float> out(N * M, -1.0f);
        halide_dimension_t wd[2] = {{0, B, 1}, {0, N, B}}, ad[2] = {{0, B, 1}, {0, M, B}};
        halide_dimension_t od[2] = {{0, N, 1}, {0, M, N}};
        halide_buffer_t wb{}, ab{}, ob{};
        wb.host = (uint8_t *)w.data(), wb.type = md->arguments[0].type, wb.dimensions = 2, wb.dim = wd;
        ab.host = (uint8_t *)a.data(), ab.type = md->arguments[1].type, ab.dimensions = 2, ab.dim = ad;
        ob.host = (uint8_t *)out.data(), ob.type = halide_type_t(halide_type_float, 32), ob.dimensions = 2, ob.dim = od;
        CHECK(q4_0_q8_0_mul_mat_checked(&wb, &ab, &ob) == 0);
        CHECK(out == expected);
    }
};

MulMat &gemv() {
    static MulMat m(256, 1, 1);
    return m;
}

MulMat &gemm() {
    static MulMat m(128, 64, 2);
    return m;
}

void run_pipeline(int i) {
    (i % 2 ? gemm() : gemv()).run();
}

struct ParForClosure {
    std::atomic<int> *hits;
    int min;
    int inner;
    int fail_at;
};

int inner_task(void *, int idx, uint8_t *closure) {
    ((std::atomic<int> *)closure)[idx]++;
    return 0;
}

int outer_task(void *user_context, int idx, uint8_t *closure) {
    ParForClosure *c = (ParForClosure *)closure;
    if (idx == c->fail_at) {
        return halide_error_code_generic_error;
    }
    c->hits[idx - c->min]++;
    if (c->inner > 0) {
        // Nested parallelism.
        std::vector<std::atomic<int>> inner_hits(c->inner);
        int ret = halide_do_par_for(user_context, inner_task, 0, c->inner, (uint8_t *)inner_hits.data());
        if (ret != 0) {
            return ret;
        }
        for (auto &h : inner_hits) {
            if (h != 1) {
                return halide_error_code_generic_error;
            }
        }
    }
    return 0;
}

void run_par_for(int n, int inner = 0, int min = 0) {
    std::vector<std::atomic<int>> hits(n);
    ParForClosure c{hits.data(), min, inner, min - 1};
    CHECK(halide_do_par_for(nullptr, outer_task, min, n, (uint8_t *)&c) == 0);
    for (auto &h : hits) {
        CHECK(h == 1);
    }

    // Errors must still propagate.
    if (n > 1) {
        c.fail_at = min + n / 2;
        CHECK(halide_do_par_for(nullptr, outer_task, min, n, (uint8_t *)&c) == halide_error_code_generic_error);
    }
}

constexpr int pc_size = 64;

struct ProducerConsumerState {
    halide_semaphore_t sem;
    int data[pc_size];
    int consumed[pc_size];
};

int producer_task(void *user_context, int min, int extent, uint8_t *closure, void *) {
    ProducerConsumerState *s = (ProducerConsumerState *)closure;
    for (int i = min; i < min + extent; i++) {
        if (i % 16 == 0) {
            // Some nested parallelism inside a task.
            std::vector<std::atomic<int>> inner_hits(8);
            if (halide_do_par_for(user_context, inner_task, 0, 8, (uint8_t *)inner_hits.data()) != 0) {
                return halide_error_code_generic_error;
            }
        }
        s->data[i] = 3 * i + 1;
        halide_semaphore_release(&s->sem, 1);
    }
    return 0;
}

int consumer_task(void *, int min, int extent, uint8_t *closure, void *) {
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
    run_par_for(1 + i % 13, 0, i % 3 - 1);
    if (i % 4 == 0) {
        run_par_for(6, 3);
        run_producer_consumer();
    }
}

void test_counting() {
    CHECK(keep_awake_count() == 0);
    CHECK(ggml_halide_thread_pool_keep_awake(true) == 1);
    CHECK(ggml_halide_thread_pool_keep_awake(true) == 2);
    CHECK(ggml_halide_thread_pool_keep_awake(false) == 1);
    CHECK(ggml_halide_thread_pool_keep_awake(false) == 0);
    {
        ThreadPoolKeepAwake a;
        CHECK(keep_awake_count() == 1);
        {
            ThreadPoolKeepAwake b;
            CHECK(keep_awake_count() == 2);
        }
        CHECK(keep_awake_count() == 1);
    }
    CHECK(keep_awake_count() == 0);
}

void test_unbalanced_release() {
    halide_error_handler_t old_handler = halide_set_error_handler(record_error);
    errors_reported = 0;
    CHECK(ggml_halide_thread_pool_keep_awake(false) == halide_error_code_generic_error);
    CHECK(errors_reported == 1);
    CHECK(last_error.find("keep_awake") != std::string::npos);
    // The count stays at zero, and works as normal afterwards.
    CHECK(ggml_halide_thread_pool_keep_awake(true) == 1);
    CHECK(ggml_halide_thread_pool_keep_awake(false) == 0);
    CHECK(keep_awake_count() == 0);
    run_everything(0);
    halide_set_error_handler(old_handler);
}

void test_many_small_loops() {
    // Alternate between holding and not holding the count, with and without
    // serial gaps between the loops, including one long enough for idle
    // threads to give up and go to sleep anyway.
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
    // the A team, and acquiring the count wakes workers that a small loop
    // already demoted. Mix small and big loops around acquiring and releasing
    // the count.
    halide_set_num_threads(0);
    const int threads = halide_get_num_threads();
    for (int round = 0; round < 20; round++) {
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
                    run_pipeline(i / 10);
                }
            }
            run_par_for(1);
        }
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

int producer_consumer_task(void *, int, uint8_t *) {
    run_producer_consumer();
    return 0;
}

struct UnevenClosure {
    std::atomic<int> *hits;
    int min;
    int fail_mod;
};

// An iteration that takes a varying time, and fails (with a code naming the
// iteration) if idx % fail_mod == 1.
int uneven_task(void *, int idx, uint8_t *closure) {
    UnevenClosure *c = (UnevenClosure *)closure;
    c->hits[idx - c->min]++;
    serial_gap((idx * 7) % 5);
    if (c->fail_mod && (idx % c->fail_mod + c->fail_mod) % c->fail_mod == 1) {
        return -1000 - idx;
    }
    return 0;
}

void test_fast_path() {
    // Back-to-back top-level loops on an idle pool with the count held take
    // the lock-free fast path. Check iteration coverage, error propagation,
    // and nested loops (which take the usual path) inside its iterations.
    halide_set_num_threads(0);
    const int threads = halide_get_num_threads();
    ThreadPoolKeepAwake keep_awake;
    run_par_for(threads);
    const unsigned long long fast_before = ggml_halide_thread_pool_fast_loops();
    std::mt19937 rng(2);
    for (int i = 0; i < 2000; i++) {
        const int n = i % 10 == 0 ? 1 + (int)(rng() % 1000) : 1 + (int)(rng() % (2 * threads));
        const int min = (int)(rng() % 7) - 3;
        const int fail_mod = i % 7 == 0 ? 1 + (int)(rng() % 5) : 0;
        std::vector<std::atomic<int>> hits(n);
        UnevenClosure c{hits.data(), min, fail_mod};
        const int ret = halide_do_par_for(nullptr, uneven_task, min, n, (uint8_t *)&c);
        bool any_fails = false;
        for (int j = 0; j < n; j++) {
            const int idx = min + j;
            any_fails |= fail_mod && (idx % fail_mod + fail_mod) % fail_mod == 1;
            // No iteration runs twice.
            CHECK(hits[j] <= 1);
        }
        if (any_fails) {
            // The error is one of the failing iterations', which ran.
            const int idx = -1000 - ret;
            CHECK(idx >= min && idx < min + n);
            CHECK((idx % fail_mod + fail_mod) % fail_mod == 1);
            CHECK(hits[idx - min] == 1);
        } else {
            CHECK(ret == 0);
            for (auto &h : hits) {
                CHECK(h == 1);
            }
        }
        if (i % 50 == 0) {
            run_par_for(threads, 2 * threads);
            run_pipeline(i / 50);
            // do_parallel_tasks with semaphores inside fast-path iterations.
            CHECK(halide_do_par_for(nullptr, producer_consumer_task, 0, threads, nullptr) == 0);
        }
        if (i % 100 == 0) {
            serial_gap(200);
        }
    }
    CHECK(ggml_halide_thread_pool_fast_loops() > fast_before || threads == 1);

    // Shut down right after a fast-path loop, while its helpers are polling,
    // and use the fast path again after restarting.
    for (int i = 0; i < 5; i++) {
        run_par_for(threads);
        halide_shutdown_thread_pool();
        for (int j = 0; j < 20; j++) {
            run_par_for(threads, j % 5 == 0 ? 3 : 0);
        }
    }

    // Concurrent top-level callers: at most one at a time uses the fast path.
    std::vector<std::thread> users;
    for (int t = 0; t < 3; t++) {
        users.emplace_back([t, threads]() {
            for (int i = 0; i < 200; i++) {
                run_par_for(1 + (i + t) % (2 * threads), (i % 20 == 0) ? 3 : 0);
            }
        });
    }
    for (auto &u : users) {
        u.join();
    }
}

struct LateClosure {
    std::atomic<int> *hits;
    int min;
    int sleep_mod;
    int fail_at;
};

// An iteration that, if idx % sleep_mod == 0, sleeps long enough for the OS to
// run something else on its core, so that the thread running it is late to
// claim anything more.
int late_task(void *, int idx, uint8_t *closure) {
    LateClosure *c = (LateClosure *)closure;
    c->hits[idx - c->min]++;
    if (c->sleep_mod && idx % c->sleep_mod == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return idx == c->fail_at ? halide_error_code_generic_error : 0;
}

// A loop of up to 3 * threads iterations of late_task, some of which sleep and
// one of which may fail, then a gap.
void run_late_loop(int threads, std::mt19937 &rng, int i) {
    const int n = 1 + (int)(rng() % (3 * threads));
    const int min = (int)(rng() % 5) - 2;
    std::vector<std::atomic<int>> hits(n);
    LateClosure c{hits.data(), min, i % 3 ? 2 + (int)(rng() % 9) : 0,
                  i % 11 == 0 ? min + (int)(rng() % n) : min - 1};
    const int ret = halide_do_par_for(nullptr, late_task, min, n, (uint8_t *)&c);
    if (c.fail_at >= min) {
        CHECK(ret == halide_error_code_generic_error);
        CHECK(hits[c.fail_at - min] == 1);
        for (auto &h : hits) {
            CHECK(h <= 1);
        }
    } else {
        CHECK(ret == 0);
        for (auto &h : hits) {
            CHECK(h == 1);
        }
    }
    if (i % 25 == 0) {
        run_par_for(threads, 3);
    }
    serial_gap(i % 4 == 0 ? 20 * (i % 3) : 0);
}

void test_late_helpers() {
    // Loops whose helpers are late or never come: with HL_NUM_THREADS from 1
    // to more threads than cores, with other threads hogging every core, with
    // iterations that sleep, with the keep-awake count held or not, and with
    // the thread count lowered below the number of workers. Every iteration
    // must run exactly once, and every loop must finish.
    const char *old_env = getenv("HL_NUM_THREADS");
    const std::string saved = old_env ? old_env : "";
    const int cores = (int)std::thread::hardware_concurrency();
    std::mt19937 rng(3);
    for (const char *env : {"1", "2", "3", "7", "64"}) {
        setenv("HL_NUM_THREADS", env, 1);
        halide_shutdown_thread_pool();
        halide_set_num_threads(0);
        const int threads = halide_get_num_threads();
        CHECK(threads == atoi(env));
        for (int hogs : {0, cores}) {
            std::atomic<bool> stop{false};
            std::vector<std::thread> hog;
            for (int t = 0; t < hogs; t++) {
                hog.emplace_back([&]() {
                    while (!stop) {
                    }
                });
            }
            for (bool hold : {true, false}) {
                if (hold) {
                    ggml_halide_thread_pool_keep_awake(true);
                }
                const unsigned long long fast_before = ggml_halide_thread_pool_fast_loops();
                for (int i = 0; i < 200; i++) {
                    run_late_loop(threads, rng, i);
                }
                if (hold) {
                    CHECK(hogs || threads == 1 || threads > cores ||
                          ggml_halide_thread_pool_fast_loops() > fast_before);
                    ggml_halide_thread_pool_keep_awake(false);
                }
            }
            stop = true;
            for (auto &h : hog) {
                h.join();
            }
        }
        if (threads > 2) {
            // Fewer threads than workers, so that some workers that join a
            // loop have no iterations of their own.
            const int fewer = 2 + threads / 3;
            halide_set_num_threads(fewer);
            ggml_halide_thread_pool_keep_awake(true);
            for (int i = 0; i < 200; i++) {
                run_late_loop(fewer, rng, i);
            }
            ggml_halide_thread_pool_keep_awake(false);
            halide_set_num_threads(threads);
        }
    }
    if (old_env) {
        setenv("HL_NUM_THREADS", saved.c_str(), 1);
    } else {
        unsetenv("HL_NUM_THREADS");
    }
    halide_shutdown_thread_pool();
    halide_set_num_threads(0);
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
    test_fast_path();
    test_late_helpers();
    test_concurrent();
    CHECK(keep_awake_count() == 0);
    printf("Success!\n");
    return 0;
}
