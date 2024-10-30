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

    std::map<std::tuple<bool, bool, int, int>, std::vector<float>> results;

    auto bench = [&](bool m, bool c, int i, int o) {
        const int memory_limit = m ? max_memory : 128;

        auto now = std::chrono::high_resolution_clock::now;
        auto to_ns = [](auto delta) { return 1e9 * std::chrono::duration<float>(delta).count(); };

        auto bench_one = [&]() {
            auto t1 = now();
            callable(i, o, memory_limit, in, out);
            auto t2 = now();
            return to_ns(t2 - t1) / (i * o);
        };

        const int num_tasks = 8;
        const int min_samples = 32;

        std::vector<float> times[num_tasks];
        if (c) {
            Halide::Tools::ThreadPool<void> thread_pool;
            Halide::Internal::JITSharedRuntime::set_num_threads(num_tasks * native_threads);
            std::vector<std::future<void>> futures(num_tasks);
            for (size_t t = 0; t < futures.size(); t++) {
                futures[t] = thread_pool.async(
                    [&](size_t t) {
                        bench_one();
                        auto t_start = now();
                        while (to_ns(now() - t_start) < 1e7 || times[t].size() < min_samples / num_tasks) {
                            times[t].push_back(bench_one());
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
            auto t_start = now();
            while (to_ns(now() - t_start) < 1e7 || times[0].size() < min_samples) {
                times[0].push_back(bench_one());
            }
        }

        std::vector<float> &r = results[{m, c, i, o}];
        for (int i = 0; i < num_tasks; i++) {
            r.insert(r.end(), times[i].begin(), times[i].end());
        }
    };

    // The output is designed to be copy-pasted into a spreadsheet, not read by a human
    printf("memory_bound contended inner outer num_samples 10%% 20%% 30%% 40%% 50%% 60%% 70%% 80%% 90%%\n");
    for (int repeat = 0; repeat < 10; repeat++) {
        for (bool contended : {false, true}) {
            for (bool memory_bound : {false, true}) {
                for (int i : {1 << 6, 1 << 9, 1 << 12, 1 << 15}) {
                    for (int o : {1, 2, 4, 8, 16, 32, 64, 128, 256}) {
                        bench(memory_bound, contended, i, o);
                    }
                }
            }
        }
    }

    for (auto p : results) {
        auto &times = p.second;
        std::sort(times.begin(), times.end());
        auto [m, c, i, o] = p.first;
        printf("%d %d %d %d %d ", m, c, i, o, (int)times.size());
        for (int decile = 10; decile <= 90; decile += 10) {
            printf("%g ", times[(decile * times.size()) / 100]);
        }
        printf("\n");
    }

    printf("Success!\n");

    return 0;
}
