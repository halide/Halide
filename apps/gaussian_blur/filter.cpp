#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>

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

// An openmp-based do-par-for, for comparing the Halide thread pool to a simple
// static partition with openmp.
#ifdef _OPENMP
int static_do_par_for(void *user_context, halide_task_t f, int min, int extent, uint8_t *closure) {
#pragma omp parallel for schedule(static)
    for (int i = min; i < min + extent; i++) {
        f(user_context, i, closure);
    }
    return 0;
}
#endif

std::atomic<int> pending{0};
std::atomic<bool> shutdown{false};
struct worker {
    std::thread thread;
    halide_task_t task = nullptr;
    int first = 0;
    int last = 0;
    uint8_t *closure = nullptr;

    // When go is false, the main thread is messing with the fields above. When
    // go is true, they are frozen.
    std::atomic<bool> go{false};

    void run() {
        while (!shutdown.load()) {
            if (!go.load()) continue;
            for (int i = first; i < last; i++) {
                task(nullptr, i, closure);
                pending--;
            }
            go.store(false);
        }
    }

    worker(int i) : thread([=]() { run(); }) {
    }
};

// Use a deque rather than a std::vector, because a worker contains an atomic
// field and you can't just move those around in memory. A deque keeps things at
// the same address.
std::deque<worker> workers;

void init_thread_pool() {
    // We want to use however many worker threads Halide would, so run a trivial
    // for loop and check how many it decided to use.
    halide_default_do_par_for(nullptr, [](void *, int, uint8_t *) { return 0; }, 0, 256, nullptr);
    int num_workers = halide_get_num_threads() - 1;  // -1 because the main thread will also work
    printf("%d worker threads\n", num_workers);
    for (int i = 0; i < num_workers; i++) {
        workers.emplace_back(i);
    }
}

void shutdown_thread_pool() {
    shutdown.store(true);
    for (auto &w : workers) {
        w.thread.join();
    }
}

int lock_free_do_par_for(void *user_context, halide_task_t f, int min, int extent, uint8_t *closure) {
    int num_workers = (int)workers.size();
    int tasks_per_worker = (extent + num_workers) / (num_workers + 1);
    pending.store(extent - tasks_per_worker);
    for (int w = 0, i = tasks_per_worker; w < num_workers; w++, i += tasks_per_worker) {
        auto &worker = workers[w];
        while (worker.go.load() == true) {}
        worker.task = f;
        worker.first = min + i;
        worker.last = std::min(worker.first + tasks_per_worker, min + extent);
        worker.closure = closure;
        worker.go.store(true);
    }
    for (int i = 0; i < tasks_per_worker; i++) {
        f(nullptr, min + i, closure);
    }
    while (pending.load() > 0) {
    }
    return 0;
}

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

    // Enable to use openmp instead of Halide's built-in threadpool
#ifdef _OPENMP
    if (false) {
        halide_set_custom_do_par_for(static_do_par_for);
    }
#endif

    halide_set_custom_do_par_for(lock_free_do_par_for);
    init_thread_pool();

    // Convert from seconds to rounded microseconds
    auto to_us = [](double seconds) {
        return (int)std::round(seconds * 1e6);
    };

    std::map<std::tuple<int, int, int>, decltype(&gaussian_blur_direct)> fns = {
#define REG(U, D, F) {{F, U, D}, &gaussian_blur_##U##_##D##_##F},
#define REG1(U, D) FOR_ALL_FACTORS(U, D, REG)
#define REG2(U) FOR_ALL_DOWNSAMPLE_ORDERS(U, REG1)
        FOR_ALL_UPSAMPLE_ORDERS(REG2)};

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

    Buffer<float> impulse(2048, 16), impulse_response(2048, 16);
    impulse.fill(0.f);
    for (int i = 0; i < 16; i++) {
        impulse(1024 - i, i) = 1.f;
    }

    // Go out to 8 * sigma on each side for ground truth.
    gaussian_blur_direct(input, sigma, 8, reference_output);

    // Ground truth impulse response
    for (int i = 0; i < impulse.height(); i++) {
        gaussian_blur_direct(impulse.cropped(1, i, 1),
                             sigma, 8,
                             impulse_response.cropped(1, i, 1));
        std::string filename = ("impulse_response_" +
                                std::to_string(sigma) + ".tiff");
        auto sheared = impulse_response;
        sheared.raw_buffer()->dim[1].stride--;
        sheared.crop(0, 1024 - 4 * sigma, 8 * sigma);
        save_image(sheared, filename.c_str());
    }

    std::vector<Result> results;
    for (int trunc : {3, 4, 5}) {
        double direct_time = benchmark([&]() {
            direct_output.device_sync();
            gaussian_blur_direct(input, sigma, trunc, direct_output);
        });
        double PSNR = compute_PSNR(direct_output, reference_output);
        printf("Direct (sigma=%s, radius=%d): %d us %g db\n", sigma_str,
               (int)std::ceilf(trunc * sigma), to_us(direct_time), PSNR);
        results.emplace_back(Result{"Direct " + std::to_string(trunc) + " sigma", direct_time, PSNR});
    }

    for (const auto &[config, fn] : fns) {
        const int up_order = std::get<1>(config);
        const int down_order = std::get<2>(config);
        const int factor = std::get<0>(config);

        // The variance of one of our resampling filters
        auto var = [=](int o) {
            // Our resampling filters are o boxes of size f, and o - 1 [1 1]
            // filters to pad out to o * f.
            return o * ((float)factor * factor - 1) / 12 + (o - 1) / 4.0f;
        };
        double variance_from_resampling = var(up_order) + var(down_order);
        if (variance_from_resampling >= sigma * sigma) {
            // It's resampling by too much to hit the desired blur
            continue;
        }

        // If we're profiling, we'd prefer to profile as we go,
        // rather than once at the end.
        halide_profiler_reset();

        auto f = fn;
        
        double resample_time = benchmark([&]() {
            f(input, sigma, 5, resample_output);
            resample_output.device_sync();
        });

        // Enable to inspect output
        if (false) {
            std::string filename = "gaussian_blur_" + std::to_string(sigma) + ".png";
            convert_and_save_image(resample_output, filename.c_str());
        }

        double PSNR = compute_PSNR(resample_output, reference_output);

        // Enable to dump impulse responses for inspection
        if (false) {
            for (int i = 0; i < impulse.height(); i++) {
                auto impulse_slice = impulse.cropped(1, i, 1);
                auto response_slice = impulse_response.cropped(1, i, 1);
                // Broadcast it vertically by enough to satisfy the schedule's requirements
                impulse_slice.raw_buffer()->dim[1].min = 0;
                impulse_slice.raw_buffer()->dim[1].stride = 0;
                impulse_slice.raw_buffer()->dim[1].extent = 16;
                response_slice.raw_buffer()->dim[1].min = 0;
                response_slice.raw_buffer()->dim[1].stride = 0;
                response_slice.raw_buffer()->dim[1].extent = 16;
                fns[{up_order, down_order, factor}](impulse_slice,
                                                    sigma, 5,
                                                    response_slice);
            }
            std::string filename = ("impulse_response_" +
                                    std::to_string(sigma) + "_" +
                                    std::to_string(up_order) + "_" +
                                    std::to_string(down_order) + "_" +
                                    std::to_string(factor) + ".tiff");
            auto sheared = impulse_response;
            sheared.raw_buffer()->dim[1].stride--;
            sheared.crop(0, 1024 - 4 * sigma, 8 * sigma);
            save_image(sheared, filename.c_str());
        }

        printf("Approx (sigma=%s up_order=%d down_order=%d factor=%d): %d us %g db\n",
               sigma_str, up_order, down_order, factor, to_us(resample_time), PSNR);
        results.emplace_back(
            Result{("up_order=" + std::to_string(up_order) +
                    " down_order=" + std::to_string(down_order) +
                    " factor=" + std::to_string(factor)),
                   resample_time,
                   PSNR});
        halide_profiler_report(nullptr);
    }

    // Print the pareto-dominant options.

    // In theory, quantization to 16-bit produces a psnr of ~100, and
    // quantization to 8-bit produces a PSNR of ~60. We'll consider anything
    // above that upper limit good, and anything below the lower limit
    // unacceptable.

    // The expected error from quantization is half a level, making
    // theoretical PSNR easy to compute:
    double min_PSNR = -20 * log10(0.5 / 255.0);

    printf("-------------------------------------\n");
    printf("Pareto-dominant options for sigma=%s:\n", sigma_str);
    // Sort by decreasing PSNR
    std::sort(results.begin(), results.end(), [=](const Result &a, const Result &b) {
        return a.PSNR > b.PSNR;
    });
    double best_time = 1e9;
    for (const Result &r : results) {
        if (r.PSNR < min_PSNR) {
            break;
        }
        // PSNR is getting worse as we iterate, so the Pareto-dominant
        // options are the ones that are the fastest seen.
        if (r.time < best_time) {
            printf(" %s: %d us %g db\n", r.name.c_str(), to_us(r.time), r.PSNR);
            best_time = r.time;
        }
    }
    printf("-------------------------------------\n");

    shutdown_thread_pool();

    return 0;
}
