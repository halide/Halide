#include "Halide.h"
#include <iostream>

using namespace Halide;

void check_error(bool error) {
    if (!error) {
        std::cout << "There was supposed to be an error!\n";
        exit(1);
    }
}

int main(int argc, char **argv) {
#if HALIDE_WITH_EXCEPTIONS
    if (!Halide::exceptions_enabled()) {
        std::cout << "[SKIP] Halide was compiled without exceptions.\n";
        return 0;
    }

    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        std::cout << "[SKIP] No GPU target enabled.\n";
        return 0;
    }

    Var v0, v1, v2, v3, v4, v5, v6, v7;
    Param<bool> p;
    for (int i = 0;; i++) {
        Func f{"f"}, g{"g"};
        f(v0, v1, v2, v3, v4, v5, v6, v7) = v0;
        g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7);
        switch (i) {
        case 0:
            // Threads but no blocks on an output Func
            g.gpu_threads(v0);
            break;
        case 1:
            // Threads but no blocks on a compute_root non-output Func
            f.compute_root().gpu_threads(v0);
            g.gpu_blocks(v1).gpu_threads(v0);
            break;
        case 2:
            // Too many blocks loops
            g.gpu_blocks(v0, v1).gpu_blocks(v2, v3);
            break;
        case 3:
            // Too many threads loops
            g.gpu_threads(v0, v1).gpu_threads(v2, v3).gpu_blocks(v4);
            break;
        case 4:
            // Threads outside of blocks
            g.gpu_blocks(v0).gpu_threads(v1);
            break;
        case 5:
            // Something with a blocks loop compute_at inside something else with a blocks loop
            g.gpu_blocks(v0);
            f.compute_at(g, v0).gpu_blocks(v0);
            break;
        case 6:
            // Something compute_at between two gpu_blocks loops
            g.gpu_blocks(v0, v1);
            f.compute_at(g, v1);
            break;
        case 7:
            // Something with too many threads loops once nesting is taken into account
            g.gpu_threads(v0, v1).gpu_blocks(v2, v3);
            f.compute_at(g, v0).gpu_threads(v0, v1);
            break;
        case 8:
            // The same, but only in a specialization
            g.gpu_threads(v0, v1).gpu_blocks(v2, v3);
            f.compute_at(g, v0).gpu_threads(v0).specialize(p).gpu_threads(v1);
            break;
        case 9:
            // A serial loop in between two gpu blocks loops
            g.gpu_blocks(v5, v7);
            break;
        default:
            std::cout << "Success!\n";
            return 0;
        }

        bool error = false;
        try {
            g.compile_jit();
        } catch (const Halide::CompileError &e) {
            error = true;
            std::cout << "Expected compile error:\n"
                      << e.what() << "\n";
        }

        if (!error) {
            printf("There should have been an error\n");
            return 1;
        }
    }

    // unreachable
#else
    std::cout << "[SKIP] Halide was compiled without exceptions.\n";
    return 0;
#endif
}
