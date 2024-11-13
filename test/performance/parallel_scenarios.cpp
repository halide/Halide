#include "Halide.h"
#include "halide_thread_pool.h"

using namespace Halide;

int main(int argc, char **argv) {
    Param<int> inner_iterations, outer_iterations, memory_limit;
    ImageParam input(Float(32), 1);

    Func f, g;
    Var x;

    RDom r(0, inner_iterations);
    // Make an inner loop with a floating point sqrt, some integer
    // multiply-adds, and a random int generation, and a random memory access.
    f(x) = sum(sqrt(input(random_int(r) % memory_limit)));

    g() = f(0) + f(outer_iterations - 1);

    f.compute_root().parallel(x);

    auto out = Runtime::Buffer<float>::make_scalar();
    const int max_memory = 100 * 1024 * 1024;
    Runtime::Buffer<float> in(max_memory);
    in.fill(17.0f);

    auto callable = g.compile_to_callable({inner_iterations, outer_iterations, memory_limit, input});

    // We want the full distribution of runtimes, not the denoised min, so we
    // won't use Tools::benchmark here.

    int native_threads = Halide::Internal::JITSharedRuntime::get_num_threads();

    auto bench = [&](bool m, bool c, int i, int o) {
        const int num_samples = 128;
        const int memory_limit = m ? max_memory : 128;

        auto bench_one = [&]() {
            auto t1 = std::chrono::high_resolution_clock::now();
            // Ignore error code because default halide_error() will abort on failure
            (void)callable(i, o, memory_limit, in, out);
            auto t2 = std::chrono::high_resolution_clock::now();
            return 1e9 * std::chrono::duration<float>(t2 - t1).count() / (i * o);
        };

        std::vector<float> times(num_samples);
        if (c) {
            Halide::Tools::ThreadPool<void> thread_pool;
            const int num_tasks = 8;
            const int samples_per_task = num_samples / num_tasks;
            Halide::Internal::JITSharedRuntime::set_num_threads(num_tasks * native_threads);
            std::vector<std::future<void>> futures(num_tasks);
            for (size_t t = 0; t < futures.size(); t++) {
                futures[t] = thread_pool.async(
                    [&](size_t t) {
                        bench_one();
                        for (int s = 0; s < samples_per_task; s++) {
                            size_t idx = t * samples_per_task + s;
                            times[idx] = bench_one();
                        }
                    },
                    t);
            }
            for (auto &f : futures) {
                f.get();
            }
        } else {
            Halide::Internal::JITSharedRuntime::set_num_threads(native_threads);
            bench_one();
            for (int s = 0; s < num_samples; s++) {
                times[s] = bench_one();
            }
        }
        std::sort(times.begin(), times.end());
        printf("%d %d %d %d ", m, c, i, o);
        const int n = 8;
        int off = (num_samples / n) / 2;
        for (int i = 0; i < n; i++) {
            printf("%g ", times[off + (num_samples * i) / n]);
        }
        printf("\n");
    };

    // The output is designed to be copy-pasted into a spreadsheet, not read by a human
    printf("memory_bound contended inner outer t0 t1 t2 t3 t4 t5 t7\n");
    for (bool contended : {false, true}) {
        for (bool memory_bound : {false, true}) {
            for (int i : {1 << 0, 1 << 6, 1 << 12, 1 << 18}) {
                for (int o : {1, 2, 4, 8, 16, 32, 64, 128, 256}) {
                    bench(memory_bound, contended, i, o);
                }
            }
        }
    }

    printf("Success!\n");

    return 0;
}
