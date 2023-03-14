#include "Halide.h"

using namespace Halide;

int num_vector_stores = 0;
int num_scalar_stores = 0;
int my_trace(JITUserContext *user_context, const halide_trace_event_t *e) {
    if (e->event == halide_trace_store) {
        if (e->type.lanes > 1) {
            num_vector_stores++;
        } else {
            num_scalar_stores++;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    for (TailStrategy tail_strategy : {TailStrategy::GuardWithIf, TailStrategy::Predicate}) {
        Func f;
        Var x;

        f(x) = x;

        const int w = 100, v = 8;
        f.vectorize(x, v, tail_strategy);
        const int expected_vector_stores = w / v;
        const int expected_scalar_stores = w % v;

        f.jit_handlers().custom_trace = &my_trace;
        f.trace_stores();

        num_vector_stores = 0;
        num_scalar_stores = 0;
        Buffer<int> result = f.realize({w});

        if (num_vector_stores != expected_vector_stores) {
            printf("There were %d vector stores instead of %d\n",
                   num_vector_stores, expected_vector_stores);
            return 1;
        }

        if (num_scalar_stores != expected_scalar_stores) {
            printf("There were %d scalar stores instead of %d\n",
                   num_vector_stores, w % 8);
            return 1;
        }

        for (int i = 0; i < w; i++) {
            if (result(i) != i) {
                printf("result(%d) == %d instead of %d\n",
                       i, result(i), i);
                return 1;
            }
        }
    }

    for (TailStrategy tail_strategy : {TailStrategy::GuardWithIf, TailStrategy::Predicate}) {
        const int w = 98, v = 8;

        Buffer<int> b(w / 2);
        for (int i = 0; i < w / 2; i++) {
            b(i) = i;
        }
        Func f;
        Var x;

        f(x) = b(x / 2) + x / 2;

        f.output_buffer().dim(0).set_min(0).set_extent(w);

        f.vectorize(x, v, tail_strategy);

        Buffer<int> result = f.realize({w});

        for (int i = 0; i < w; i++) {
            if (result(i) != i / 2 + i / 2) {
                printf("result(%d) == %d instead of %d\n",
                       i, result(i), i);
                return 1;
            }
        }
    }

    {
        Var x;
        Func f, g;

        ImageParam in(Int(32), 1);

        Expr index = clamp(x * x - 2, 0, x);

        f(x) = index + in(index);
        g(x) = f(x);

        f.compute_root().vectorize(x, 8, TailStrategy::PredicateLoads);
        g.compute_root().vectorize(x, 8, TailStrategy::PredicateStores);

        const int w = 100;
        Buffer<int> buf(w);
        buf.fill(0);
        in.set(buf);
        Buffer<int> result = g.realize({w});

        for (int i = 0; i < w; i++) {
            int correct = std::max(std::min(i * i - 2, i), 0);
            if (result(i) != correct) {
                printf("result(%d) == %d instead of %d\n",
                       i, result(i), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
