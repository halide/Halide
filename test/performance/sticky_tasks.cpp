#include "Halide.h"

#include <unistd.h>

using namespace Halide;

double speed_up(int working_set_size_kb, int tasks) {
    Buffer<float> buf(working_set_size_kb * 256, tasks);
    buf.fill(17.0f);

    Func f;
    Var x, y;

    // Load, modify, store in-place with a serial outer loop
    f(x, y) = undef<float>();
    RDom r(0, 100);
    f(x, y) += cast<float>(r);

    f.update().reorder(x, y, r).vectorize(x, 16).parallel(y);
    f.realize(buf);

    double sticky_sum = 0.0, greedy_sum = 0.0;
    for (int j = 0; j < 3; j++) {
        for (bool s : {false, true}) {

            Halide::Internal::JITSharedRuntime::set_sticky_threads(s);
            double total_time = 0.0;
            int inner_count = 0;
            while (total_time < 0.1 || inner_count < 3) {
                auto t1 = std::chrono::high_resolution_clock::now();
                f.realize(buf);
                auto t2 = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> diff = t2 - t1;
                double t = diff.count();
                total_time += t;
                inner_count++;
            }
            if (s) {
                sticky_sum += total_time / inner_count;
            } else {
                greedy_sum += total_time / inner_count;
            }
        }
    }

    return greedy_sum / sticky_sum;
}

int main(int argc, char **argv) {
    printf("Working set size (KB), number of tasks, speed-up\n");
    for (int wss : {24, 48, 96, 192, 384, 768, 1536, 3072, 6144}) {
        for (int tasks : {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64}) {
            printf("%d %d %f\n", wss, tasks, speed_up(wss, tasks));
        }
    }
    return 0;

    /*
    // One row of this buffer mostly fills the L2 cache of a core on my
    // machine. You can't fit two rows of it. There will be 16 rows because I am
    // using 16 threads.
    Buffer<float> buf(1024 * 100, 20);
    buf.fill(17.0f);

    Func f;
    Var x, y;

    // Load, modify, store in-place.
    f(x, y) = undef<float>();
    f(x, y) += 1.0f;

    f.update().vectorize(x, 16).parallel(y);
    f.compile_jit();

    std::vector<double> greedy_times, sticky_times;

    for (int j = 0; j < 10; j++) {
        for (bool s : {false, true}) {

            Halide::Internal::JITSharedRuntime::set_sticky_threads(s);
            for (int i = 0; i < 1000; i++) {
                auto t1 = std::chrono::high_resolution_clock::now();
                f.realize(buf);
                auto t2 = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> diff = t2 - t1;
                if (s) {
                    sticky_times.push_back(diff.count() * 1e6);
                } else {
                    greedy_times.push_back(diff.count() * 1e6);
                }
            }
        }
    }

    std::sort(sticky_times.begin(), sticky_times.end());
    std::sort(greedy_times.begin(), greedy_times.end());

    double sticky_prod = 0.0, greedy_prod = 0.0;
    int count = 0;
    for (size_t i = 0; i < sticky_times.size(); i += 100) {
        printf("%f %f\n", sticky_times[i], greedy_times[i]);
        sticky_prod += std::log(sticky_times[i]);
        greedy_prod += std::log(greedy_times[i]);
        count++;
    }
    sticky_prod /= count;
    greedy_prod /= count;
    printf("geomean speed-up: %f\n", std::exp(greedy_prod - sticky_prod));

    return 0;
    */
}
