#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <thread>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <immintrin.h>
#endif

#include "HalideBuffer.h"
#include "HalideRuntime.h"

// These lists should be kept in sync with the build, which must build at least
// what's listed here.
#define FOR_ALL_UPSAMPLE_ORDERS(X) X(2) X(3) X(4)
#define FOR_ALL_DOWNSAMPLE_ORDERS(A, X) X(A, 1) X(A, 2) X(A, 3)
#define FOR_ALL_FACTORS(A, B, X) X(A, B, 2) X(A, B, 4) X(A, B, 8) X(A, B, 16)

#include "blurs.h"

#include "halide_benchmark.h"
#include "halide_image_io.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

// The built-in Halide thread pool is terrible for this app because there are
// few tasks and they are very small. The workers serialize fighting over access
// to the task queue. Here we define a custom thread pool for this app. Work is
// statically partitioned. Threads are assigned work by the main thread by
// setting some per-thread fields protected by atomics. Workers access minimal
// shared state, and workers never sleep. You absolutely wouldn't want to use
// this thread pool in production, because it soaks up all available CPU cycles
// whether or not Halide code is running, and static partitioning of work is not
// very robust to things like big.little systems. However, it's good for
// comparing blur configurations without just measuring thread-pool overhead.
struct LockFreeThreadPool {
    // The number of work items done on the current job (we do not support
    // nested parallelism).
    std::atomic<int> completed{0};

    // A flag to tell the workers to terminate
    std::atomic<bool> shutdown{false};

    static void pause() {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
        _mm_pause();
#elif defined(_M_ARM) || defined(_M_ARM64)
        __yield();
#elif defined(__aarch64__) || defined(__arm__)
        // GCC lacks __yield(). See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105416
        asm volatile("yield" ::: "memory");
#endif
    }

    // A worker thread
    struct Worker {
        LockFreeThreadPool *pool = nullptr;
        std::thread thread;

        // When go is false, the main thread is messing with the fields below. When
        // go is true, they are frozen.
        std::atomic<bool> go{false};

        // The task to perform, and which loop iterations this thread is
        // responsible for.
        halide_task_t task = nullptr;
        int first = 0, last = 0;
        uint8_t *closure = nullptr;

        void run() {
            auto *s = &(pool->shutdown);
            auto *c = &(pool->completed);
            while (!s->load(std::memory_order_relaxed)) {
                if (!go.load(std::memory_order_acquire)) {
                    pause();
                    continue;
                }
                for (int i = first; i < last; i++) {
                    task(nullptr, i, closure);
                }
                c->fetch_add(last - first, std::memory_order_release);
                go.store(false, std::memory_order_release);
            }
        }

        Worker(LockFreeThreadPool *p, int i) : pool(p), thread([=]() { run(); }) {
        }
    };

    // Use a deque rather than a std::vector, because a worker contains an atomic
    // field and you can't just move those around in memory. A deque keeps things at
    // the same address.
    std::deque<Worker> workers;

    LockFreeThreadPool() {
        // We want to use however many worker threads Halide would, so run a trivial
        // for loop and check how many it decided to use.
        halide_default_do_par_for(nullptr, [](void *, int, uint8_t *) { return 0; }, 0, 256, nullptr);
        int num_workers = halide_get_num_threads() - 1;  // -1 because the main thread will also work
        for (int i = 0; i < num_workers; i++) {
            workers.emplace_back(this, i);
        }
    }

    void terminate() {
        shutdown.store(true);
        for (auto &w : workers) {
            w.thread.join();
        }
        workers.clear();
    }

    ~LockFreeThreadPool() {
        terminate();
    }

    int do_par_for(void *user_context, halide_task_t f, int min, int extent, uint8_t *closure) {
        int num_workers = (int)workers.size();

        // Decide how many tasks each worker will perform, accounting for the
        // fact that this thread will also work.
        int tasks_per_worker = (extent + num_workers) / (num_workers + 1);

        completed.store(0);
        int i = 0;
        int worker_tasks = 0;
        for (int w = 0; w < num_workers; w++, i += tasks_per_worker) {
            auto &worker = workers[w];
            // Wait for the worker to be idle.
            while (worker.go.load(std::memory_order_acquire) == true) {
                pause();
            }
            // Set the task for it to perform.
            worker.task = f;
            worker.first = min + i;
            worker.last = std::min(worker.first + tasks_per_worker, min + extent);
            worker.closure = closure;
            // Release it to work.
            if (worker.last > worker.first) {
                worker_tasks += worker.last - worker.first;
                worker.go.store(true, std::memory_order_release);
            } else {
                break;
            }
        }
        // Do the rest of the work myself.
        for (; i < extent; i++) {
            f(user_context, min + i, closure);
        }
        // Wait for the workers to finish.
        while (completed.load() < worker_tasks) {
            pause();
        }
        return 0;
    }
} *global_thread_pool = nullptr;

double compute_PSNR(const Buffer<float> &a, const Buffer<float> &b) {
    double err = 0;
    uint64_t count = 0;
    for (int y = 0; y < a.height(); y++) {
        double row_err = 0;
        for (int x = 0; x < a.width(); x++) {
            double delta = a(x, y) - b(x, y);
            row_err += delta * delta;
            count++;
        }
        err += row_err;
    }
    return -10.0 * log10((double)err / count);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s input.png <sigma>\n", argv[0]);
        return 1;
    }

    Buffer<float> input = load_and_convert_image(argv[1]);
    const char *sigma_str = argv[2];
    float sigma = std::atof(sigma_str);

    Buffer<float> memcpy_output(input.width(), input.height());
    Buffer<float> direct_output(input.width(), input.height());
    Buffer<float> reference_output(input.width(), input.height());
    Buffer<float> resample_output(input.width(), input.height());
    direct_output.fill(0);
    resample_output.fill(0);
    reference_output.fill(0);

    std::map<std::tuple<int, int, int>, decltype(&gaussian_blur_direct)> fns = {
#define REG(U, D, F) {{F, U, D}, &gaussian_blur_##U##_##D##_##F},
#define REG1(U, D) FOR_ALL_FACTORS(U, D, REG)
#define REG2(U) FOR_ALL_DOWNSAMPLE_ORDERS(U, REG1)
        FOR_ALL_UPSAMPLE_ORDERS(REG2)};

    LockFreeThreadPool pool;
    global_thread_pool = &pool;
    halide_set_custom_do_par_for([](void *user_context, halide_task_t f, int min, int extent, uint8_t *closure) {
        return global_thread_pool->do_par_for(user_context, f, min, extent, closure);
    });

    // Convert from seconds to rounded microseconds
    auto to_us = [](double seconds) {
        return (int)std::round(seconds * 1e6);
    };

    // Benchmark memcpy run using the same thread pool, to get a sense of where
    // the memory bandwidth limit is.
    double memcpy_time = benchmark([&]() {
        int strip = (input.height() + 31) / 32;
        auto task = [&](int y) {
            int rows = std::min(strip, input.height() - y * strip);
            std::memcpy(&memcpy_output(0, y * strip),
                        &input(0, y * strip),
                        input.width() * rows * sizeof(float));
        };
        auto trampoline = [](void *user_context, int y, uint8_t *closure) {
            decltype(task) *t = (decltype(task) *)closure;
            (*t)(y);
            return 0;
        };
        halide_do_par_for(nullptr, trampoline, 0,
                          (input.height() + strip - 1) / strip,
                          (uint8_t *)(&task));
    });
    printf("Memcpy: %d us\n", to_us(memcpy_time));

    struct Result {
        std::string name;
        double time;
        double PSNR;
    };

    // Go out to 8 * sigma on each side for ground truth.
    gaussian_blur_direct(input, sigma, 8, reference_output);

    if (reference_output.device_dirty()) {
        reference_output.copy_to_host();
        // We're comparing GPU kernels. Turn off the CPU thread-pool.
        halide_set_custom_do_par_for(halide_default_do_par_for);
        pool.terminate();
    }

    // Direct blur, truncated at various sigma
    std::vector<Result> results;
    for (int trunc : {3, 4, 5}) {
        double direct_time = benchmark([&]() {
            gaussian_blur_direct(input, sigma, trunc, direct_output);
            direct_output.device_sync();
        });
        direct_output.copy_to_host();
        double PSNR = compute_PSNR(direct_output, reference_output);
        printf("Direct (sigma=%s, radius=%d): %d us %g db\n", sigma_str,
               (int)std::ceil(trunc * sigma), to_us(direct_time), PSNR);
        results.emplace_back(Result{"Direct " + std::to_string(trunc) + " sigma", direct_time, PSNR});
    }
    // If profiling, we're going to report as we go, rather than once at the
    // end.
    halide_profiler_report(nullptr);
    halide_profiler_reset();

    for (const auto &[config, fn] : fns) {
        const int factor = std::get<0>(config);
        const int up_order = std::get<1>(config);
        const int down_order = std::get<2>(config);

        // Capturing a structured binding is a lambda is a C++20 extension.
        auto f = fn;
        double resample_time = benchmark([&]() {
            f(input, sigma, 5, resample_output);
            resample_output.device_sync();
        });

        resample_output.copy_to_host();

        // Enable to inspect output
        if (false) {
            std::string filename = "gaussian_blur_" + std::to_string(sigma) + ".png";
            convert_and_save_image(resample_output, filename.c_str());
        }

        double PSNR = compute_PSNR(resample_output, reference_output);

        printf("Approx (sigma=%s up_order=%d down_order=%d factor=%d): %d us %g db\n",
               sigma_str, up_order, down_order, factor, to_us(resample_time), PSNR);
        results.emplace_back(
            Result{("up_order=" + std::to_string(up_order) +
                    " down_order=" + std::to_string(down_order) +
                    " factor=" + std::to_string(factor)),
                   resample_time,
                   PSNR});

        halide_profiler_report(nullptr);
        halide_profiler_reset();
    }

    // Print the pareto-dominant options.

    // For reference, quantization to 16-bit produces a psnr of ~100, and
    // quantization to 8-bit produces a PSNR of ~60. So anything above 100 is
    // overkill, and anything below 60 probably isn't good enough.

    printf("-------------------------------------\n");
    printf("Pareto-dominant options for sigma=%s:\n", sigma_str);
    // Sort by decreasing PSNR
    std::sort(results.begin(), results.end(), [=](const Result &a, const Result &b) {
        return a.PSNR > b.PSNR;
    });
    double best_time = 1e9;
    for (const Result &r : results) {
        // PSNR is getting worse as we iterate, so the Pareto-dominant
        // options are the ones that are the fastest seen.
        if (r.time < best_time) {
            printf(" %s: %d us %g db\n", r.name.c_str(), to_us(r.time), r.PSNR);
            best_time = r.time;
        }
    }
    printf("-------------------------------------\n");

    printf("Success!\n");

    return 0;
}
