#include <stdio.h>
#include "Halide.h"

using namespace Halide;

// A trace that checks for vector and scalar stores
bool run_tracer = false;
int niters_expected = 0;
int niters = 0;

int intermediate_depend_on_output_trace(void *user_context, const halide_trace_event *e) {
    if (std::string(e->func) == "g") {
        if (e->event == halide_trace_produce) {
            //printf(".....Turning on tracer for g\n");
            run_tracer = true;
        } else if (e->event == halide_trace_consume) {
            //printf(".....Turning off tracer for g\n");
            run_tracer = false;
        }

        if (run_tracer && (e->event == halide_trace_store)) {
            //printf("Running tracer on [%d, %d]\n", e->coordinates[0], e->coordinates[1]);
            if (!((e->coordinates[0] < e->coordinates[1]) && (e->coordinates[0] >= 0) &&
                  (e->coordinates[0] <= 99) && (e->coordinates[1] >= 0) &&
                  (e->coordinates[1] <= 99))) {
                printf("Bounds on store of g were supposed to be x < y and x=[0, 99] and y=[0, 99]\n"
                       "Instead they are: %d %d\n", e->coordinates[0], e->coordinates[1]);
                exit(-1);
            }
            niters++;
        }
    }
    return 0;
}

int two_linear_bounds_trace(void *user_context, const halide_trace_event *e) {
    if (std::string(e->func) == "g") {
        if (e->event == halide_trace_produce) {
            run_tracer = true;
        } else if (e->event == halide_trace_consume) {
            run_tracer = false;
        }

        if (run_tracer && (e->event == halide_trace_store)) {
            if (!((e->coordinates[0] >= 0) && (e->coordinates[0] <= 99) &&
                  (e->coordinates[1] >= 0) && (e->coordinates[1] <= 99))) {
                printf("Bounds on store of g were supposed to be x < y and x=[0, 99] and y=[0, 99]\n"
                       "Instead they are: %d %d\n", e->coordinates[0], e->coordinates[1]);
                exit(-1);
            }
            niters++;
        }
    }
    return 0;
}

int equality_inequality_bound_test() {
    Func f("f");
    Var x("x"), y("y");
    f(x, y) = x + y;

    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    r.where(!(r.x != 10));
    f(r.x, r.y) += 1;

    Image<int> im = f.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((x == 10) && (0 <= y && y <= 99)) {
                correct += (x < y) ? 1 : 0;
            }
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    return 0;
}

int split_fuse_test() {
    Func f("f");
    Var x("x"), y("y");
    f(x, y) = x + y;

    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    f(r.x, r.y) += 1;

    RVar rx_outer, rx_inner, r_fused;
    f.update().reorder(r.y, r.x);
    f.update().split(r.x, rx_outer, rx_inner, 4);
    f.update().fuse(rx_inner, r.y, r_fused);

    Image<int> im = f.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                correct += (x < y) ? 1 : 0;
            }
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    return 0;
}

int two_linear_bounds_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    g(x, y) = x + y;

    f(x, y) = x + y;
    RDom r(0, 100, 0, 100);
    r.where(2*r.x + 30 < r.y);
    r.where(r.y >= 100 - r.x);
    f(r.x, r.y) += 2*g(r.x, r.y);

    // Expect g to be still computed over x=[0,99] and y=[0,99] since the
    // bound inference is in term of boxes
    g.compute_root();

    f.set_custom_trace(&two_linear_bounds_trace);
    g.trace_stores();
    g.trace_realizations();

    run_tracer = false;
    niters_expected = 100*100;
    niters = 0;
    Image<int> im = f.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                correct = ((2*x + 30 < y) && (y >= 100 - x)) ? 3*correct : correct;
            }
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    if (niters_expected != niters) {
        printf("two_linear_bounds_test : Expect niters on g to be %d but got %d instead\n",
               niters_expected, niters);
        return -1;
    }
    return 0;
}

int free_variable_bound_test() {
    Func f("f");
    Var x("x"), y("y"), z("z");
    f(x, y, z) = x + y + z;

    RDom r(0, 100, 0, 100, "r");
    r.where(r.x < r.y + z);
    f(r.x, r.y, z) += 1;

    Image<int> im = f.realize(200, 200, 200);
    for (int z = 0; z < im.channels(); z++) {
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y + z;
                if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                    correct += (x < y + z) ? 1 : 0;
                }
                if (im(x, y, z) != correct) {
                    printf("im(%d, %d, %d) = %d instead of %d\n",
                           x, y, z, im(x, y, z), correct);
                    return -1;
                }
            }
        }
    }
    return 0;
}

int func_call_inside_bound_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    g(x) = x;

    f(x, y) = x + y;

    RDom r(0, 100, 0, 100, "r");
    r.where(r.x < g(r.y));
    f(r.x, r.y) += 1;

    g.compute_root();

    Image<int> im = f.realize(200, 200);

    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                correct += (x < y) ? 1 : 0;
            }
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    return 0;
}

int intermediate_depend_on_output_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    g(x, y) = x;
    f(x, y) = x + y;

    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    f(r.x, r.y) = g(r.x, r.y);

    g.compute_at(f, r.y);

    f.set_custom_trace(&intermediate_depend_on_output_trace);
    g.trace_stores();
    g.trace_realizations();

    run_tracer = false;
    niters_expected = 100*99/2;
    niters = 0;
    Image<int> im = f.realize(200, 200);

    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                if (x < y) {
                    correct = x;
                }
            }
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    if (niters_expected != niters) {
        printf("intermediate_depend_on_output_test: Expect niters on g to be %d but got %d instead\n",
               niters_expected, niters);
        return -1;
    }
    return 0;
}

int newton_test() {
    Func inverse;
    Var x;
    // Negating the bits of a float is a piecewise linear approximation to inverting it
    inverse(x) = {-0.25f * reinterpret<float>(~(reinterpret<uint32_t>(cast<float>(x+1)))), 0};
    const int max_iters = 10;
    RDom r(0, max_iters);
    Expr not_converged = abs(inverse(x)[0] * (x+1) - 1) > 0.001f;
    r.where(not_converged);

    // Compute the inverse of x using Newton's method, and count the
    // number of iterations required to reach convergence.
    inverse(x) = {inverse(x)[0] * (2 - (x+1) * inverse(x)[0]),
                  r+1};
    {
        Realization r = inverse.realize(128);
        Image<float> r0 = r[0];
        Image<int> r1 = r[1];
        for (int i = 0; i < r0.width(); i++) {
            float x = (i+1);
            float prod = x * r0(i);
            int num_iters = r1(i);
            if (num_iters == max_iters) {
                printf("Newton's method didn't converge!\n");
                return -1;
            }
            if (std::abs(prod - 1) > 0.001) {
                printf("Newton's method converged without producing the correct inverse:\n"
                       "%f * %f = %f (%d iterations)\n", x, r0(i), prod, r1(i));
                return -1;
            }

        }
    }
    return 0;
}

int main(int argc, char **argv) {
    printf("Running equality inequality bound test\n");
    if (equality_inequality_bound_test() != 0) {
        return -1;
    }

    printf("Running split fuse test\n");
    if (split_fuse_test() != 0) {
        return -1;
    }

    printf("Running two linear bounds test\n");
    if (two_linear_bounds_test() != 0) {
        return -1;
    }

    printf("Running bound depend on free variable test\n");
    if (free_variable_bound_test() != 0) {
        return -1;
    }

    printf("Running function call inside bound test\n");
    if (func_call_inside_bound_test() != 0) {
        return -1;
    }

    printf("Running intermediate stage depend on output bound\n");
    if (intermediate_depend_on_output_test() != 0) {
        return -1;
    }

    printf("Running newton test\n");
    if (newton_test() != 0) {
        return -1;
    }

    printf("Success!\n");

    return 0;

}
