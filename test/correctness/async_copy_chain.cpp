#include "Halide.h"

using namespace Halide;

Var x, y;

void check(Func f) {
    Buffer<int> out = f.realize({256, 256});
    out.for_each_element([&](int x, int y) {
        if (out(x, y) != x + y) {
            printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), x + y);
            exit(1);
        }
    });
}

void make_pipeline(Func &A, Func &B) {
    A(x, y) = x + y;
    B(x, y) = A(x, y);
}

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly does not support async() yet.\n");
        return 0;
    }

    // Make a list of extern pipeline stages (just copies) all async
    // and connected by double buffers, then try various nestings of
    // them. This is a stress test of the async extern storage folding
    // logic.

    // Basic double-buffered A->B, with no extern stages
    {
        Func A, B;
        make_pipeline(A, B);

        A.store_root().compute_at(B, y).fold_storage(y, 2).async();

        check(B);
    }

    // Inject a copy stage between them
    {
        Func A, B;
        make_pipeline(A, B);

        A.store_root().compute_at(B, y).fold_storage(y, 2).async();
        A.in().store_root().compute_at(B, y).fold_storage(y, 2).async().copy_to_host();

        check(B);
    }

    // Inject a copy stage between them, but nest the first stage into it
    {
        Func A, B;
        make_pipeline(A, B);

        A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
        A.in().store_root().compute_at(B, y).fold_storage(y, 2).async().copy_to_host();

        check(B);
    }

    // Two copy stages, flat
    {
        Func A, B;
        make_pipeline(A, B);

        A.store_root().compute_at(B, y).fold_storage(y, 2).async();
        A.in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();
        A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();

        check(B);
    }

    // Two copy stages, each stage nested inside the outermost var of the next
    {
        Func A, B;
        make_pipeline(A, B);

        A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
        A.in().store_root().compute_at(A.in().in(), Var::outermost()).fold_storage(y, 2).copy_to_host().async();
        A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();

        check(B);
    }

    if (get_jit_target_from_environment().has_gpu_feature()) {
        // Two copy stages, to the device and back, flat
        {
            Func A, B;
            make_pipeline(A, B);

            A.store_root().compute_at(B, y).fold_storage(y, 2).async();
            A.in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_device().async();
            A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();

            check(B);
        }

        // Two copy stages, to the device and back, each stage nested inside the outermost var of the next
        {
            Func A, B;
            make_pipeline(A, B);

            A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
            A.in().store_root().compute_at(A.in().in(), Var::outermost()).fold_storage(y, 2).copy_to_device().async();
            A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();

            check(B);
        }

        // The same, but make one of the copy stages non-extern to force a shared host-dev allocation
        {
            Func A, B;
            make_pipeline(A, B);

            A.store_root().compute_at(B, y).fold_storage(y, 2).async();
            A.in().store_root().compute_at(B, y).fold_storage(y, 2).async();
            A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();

            check(B);
        }
        {
            Func A, B;
            make_pipeline(A, B);

            A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
            A.in().store_root().compute_at(A.in().in(), Var::outermost()).fold_storage(y, 2).async();
            A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();

            check(B);
        }
        {
            Func A, B;
            make_pipeline(A, B);

            A.store_root().compute_at(B, y).fold_storage(y, 2).async();
            A.in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_device().async();
            A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).async();

            check(B);
        }

        {
            Func A, B;
            make_pipeline(A, B);

            A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
            A.in().store_root().compute_at(A.in().in(), Var::outermost()).fold_storage(y, 2).copy_to_device().async();
            A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).async();

            check(B);
        }
    }

    printf("Success!\n");
    return 0;
}
