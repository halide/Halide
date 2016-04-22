#include <stdio.h>
#include "Halide.h"

using namespace Halide;

// A trace that checks for vector and scalar stores
int buffer_index = 0;
bool run_tracer = false;
int niters_expected = 0;
int niters = 0;

int intermediate_bound_depend_on_output_trace(void *user_context, const halide_trace_event *e) {
    std::string buffer_name = "g_" + std::to_string(buffer_index);
    if (std::string(e->func) == buffer_name) {
        if (e->event == halide_trace_produce) {
            run_tracer = true;
        } else if (e->event == halide_trace_consume) {
            run_tracer = false;
        }

        if (run_tracer && (e->event == halide_trace_store)) {
            if (!((e->coordinates[0] < e->coordinates[1]) && (e->coordinates[0] >= 0) &&
                  (e->coordinates[0] <= 199) && (e->coordinates[1] >= 0) &&
                  (e->coordinates[1] <= 199))) {
                printf("Bounds on store of g were supposed to be x < y and x=[0, 99] and y=[0, 99]\n"
                       "Instead they are: %d %d\n", e->coordinates[0], e->coordinates[1]);
                exit(-1);
            }
            niters++;
        }
    }
    return 0;
}

int box_bound_trace(void *user_context, const halide_trace_event *e) {
    std::string buffer_name = "g_" + std::to_string(buffer_index);
    if (std::string(e->func) == buffer_name) {
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

int equality_inequality_bound_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index));
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

int split_fuse_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index));
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

int free_variable_bound_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index));
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

int func_call_inside_bound_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index)), g("g_" + std::to_string(index));
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

int two_linear_bounds_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index)), g("g_" + std::to_string(index));
    Var x("x"), y("y");

    g(x, y) = x + y;

    f(x, y) = x + y;
    RDom r(0, 100, 0, 100);
    r.where(2*r.x + 30 < r.y);
    r.where(r.y >= 100 - r.x);
    f(r.x, r.y) += 2*g(r.x, r.y);

    // Expect g to be still computed over x=[0,99] and y=[0,99] since the bound
    // inference is done in term of boxes (i.e. can't represent polytopes)
    g.compute_root();

    f.set_custom_trace(&box_bound_trace);
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

int circle_bound_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index)), g("g_" + std::to_string(index));
    Var x("x"), y("y");
    g(x, y) = x;
    f(x, y) = x + y;

    // Iterate over circle with radius of 10
    RDom r(0, 100, 0, 100);
    r.where(r.x*r.x + r.y*r.y <= 100);
    f(r.x, r.y) += g(r.x, r.y);

    // Expect g to be still computed over x=[0,99] and y=[0,99]. The predicate
    // guard for the non-linear term will be left as is in the inner loop of f,
    // i.e. f loop will still iterate over x=[0,99] and y=[0,99].
    g.compute_at(f, r.y);

    f.set_custom_trace(&box_bound_trace);
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
                correct += (x*x + y*y <= 100) ? x : 0;
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

int intermediate_computed_if_param_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index)), g("g_" + std::to_string(index));
    Var x("x"), y("y");
    Param<int> p;

    g(x, y) = x + y;

    f(x, y) = x + y;
    RDom r(0, 100, 0, 100);
    r.where(p > 3);
    f(r.x, r.y) += 2*g(r.x, r.y);

    // Expect g to be only computed over x=[0,99] and y=[0,99] if param is bigger
    // than 3.
    g.compute_root();

    f.set_custom_trace(&box_bound_trace);
    g.trace_stores();
    g.trace_realizations();

    {
        printf("....Set p to 5, expect g to be computed\n");
        p.set(5);
        run_tracer = false;
        niters_expected = 100*100;
        niters = 0;
        Image<int> im = f.realize(200, 200);
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
                if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                    correct = 3*correct;
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
    }

    {
        printf("....Set p to 0, expect g to be not computed\n");
        p.set(0);
        run_tracer = false;
        niters_expected = 0;
        niters = 0;
        Image<int> im = f.realize(200, 200);
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
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
    }
    return 0;
}

int intermediate_bound_depend_on_output_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index)), g("g_" + std::to_string(index));
    Var x("x"), y("y");

    g(x, y) = x;
    f(x, y) = x + y;

    RDom r(0, 200, 0, 200);
    r.where(r.x < r.y);
    f(r.x, r.y) = g(r.x, r.y);

    // Expect bound of g on r.x to be directly dependent on the simplified
    // bound of f on r.x, which should have been r.x = [0, r.y) in this case
    g.compute_at(f, r.y);

    f.set_custom_trace(&intermediate_bound_depend_on_output_trace);
    g.trace_stores();
    g.trace_realizations();

    run_tracer = false;
    niters_expected = 200*199/2;
    niters = 0;
    Image<int> im = f.realize(200, 200);

    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((0 <= x && x <= 199) && (0 <= y && y <= 199)) {
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
        printf("intermediate_bound_depend_on_output_test: Expect niters on g to be %d but got %d instead\n",
               niters_expected, niters);
        return -1;
    }
    return 0;
}

int tile_intermediate_bound_depend_on_output_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index)), g("g_" + std::to_string(index));
    Var x("x"), y("y");

    g(x, y) = x;

    f(x, y) = x + y;

    RDom r(0, 200, 0, 200, "r");
    r.where(r.x < r.y);
    f(r.x, r.y) += g(r.x, r.y);

    RVar rxi("rxi"), ryi("ryi");
    f.update(0).tile(r.x, r.y, rxi, ryi, 8, 8);
    f.update(0).reorder(rxi, ryi, r.x, r.y);

    // Expect bound of g on r.x to be directly dependent on the simplified
    // bound of f on r.x, which should have been r.x = [0, r.y) in this case
    g.compute_at(f, ryi);

    f.set_custom_trace(&intermediate_bound_depend_on_output_trace);
    g.trace_stores();
    g.trace_realizations();

    run_tracer = false;
    niters_expected = 200*199/2;
    niters = 0;
    Image<int> im = f.realize(200, 200);

    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((0 <= x && x <= 199) && (0 <= y && y <= 199)) {
                correct += (x < y) ? x : 0;
            }
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }

    if (niters_expected != niters) {
        printf("intermediate_bound_depend_on_output_test: Expect niters on g to be %d but got %d instead\n",
               niters_expected, niters);
        return -1;
    }
    return 0;
}

int newton_method_test() {
    Func inverse;
    Var x;
    // Negating the bits of a float is a piecewise linear approximation to inverting it
    inverse(x) = {-0.25f * reinterpret<float>(~(reinterpret<uint32_t>(cast<float>(x+1)))), 0};
    const int max_iters = 10;
    RDom r(0, max_iters);
    Expr not_converged = abs(inverse(x)[0] * (x+1) - 1) > 0.001f;
    r.where(not_converged);

    // Compute the inverse of x using Newton's method, and count the
    // number of iterations required to reach convergence
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

int init_on_gpu_update_on_cpu_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index));
    Var x("x"), y("y");
    f(x, y) = x + y;

    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    r.where(!(r.x != 10));
    f(r.x, r.y) += 3;

    f.gpu_tile(x, y, 4, 4);

    Image<int> im = f.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((x == 10) && (0 <= y && y <= 99)) {
                correct += (x < y) ? 3 : 0;
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

int init_on_cpu_update_on_gpu_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index));
    Var x("x"), y("y");
    f(x, y) = x + y;

    RDom r(0, 100, 0, 100);
    r.where(!(r.x != 10));
    r.where(r.x < r.y);
    f(r.x, r.y) += 3;
    //f(r.x, r.y) += select((r.x < r.y) && (r.x == 10), 3, undef<int>());

    f.update(0).gpu_tile(r.x, r.y, 4, 4);

    Image<int> im = f.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((x == 10) && (0 <= y && y <= 99)) {
                correct += (x < y) ? 3 : 0;
            }
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }

    /*int **out = new int*[200];
    for (int i = 0; i < 200; ++i) {
        out[i] = new int[200];
    }
    for (int i = 0; i < 200; ++i) {
        for (int j = 0; j < 200; ++j) {
            out[i][j] = i + j;
        }
    }
    for (int by = 0; by < 25; by++) {
        for (int bx = 0; bx < 25; bx++) {
            for (int ty = 0; ty < 4; ty++) {
                for (int tx = 0; tx < 4; tx++) {
                    int t4 = std::min(std::max((((9 - tx)/4) + 1), 0), 25);
                    int t5 = std::max(std::min((((10 - tx)/4) + 1), by), (((9 - tx)/4) + 1));

                    if (((t4 < bx) && (bx < t5))) {
                        out[bx*4+tx][by*4+ty] += 3;
                    } else if ((((bx*4) + tx) == 10)) {
                        if (((bx*4) < (((by*4) + ty) - tx))) {
                            out[10][by*4+ty] += 3;
                        }
                    }
                }
            }
        }
    }

    //NOTE: INCORRECT
    for (int by = 0; by < 25; by++) {
        for (int bx = 0; bx < 25; bx++) {
            for (int ty = 0; ty < 4; ty++) {
                for (int tx = 0; tx < 4; tx++) {
                    if ((bx < by)) {
                        int t4 = (10 - std::max(std::min((bx*4), 10), 6));
                        int t5 = std::max((11 - std::max((bx*4), 7)), (10 - std::max(std::min((bx*4), 10), 6)));
                        printf("bx: %d, by: %d, t4: %d, t5: %d\n", bx, by, t4, t5);
                        if (((t4 < bx) && (bx < t5))) {
                            out[bx*4+tx][by*4+ty] += 3;
                        }
                    } else if ((((bx*4) + tx) == 10)) {
                        if (((bx*4) < (((by*4) + ty) - tx))) {
                            out[10][by*4+ty] += 3;
                        }
                    }
                }
            }
        }
    }

    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            if (im(x, y) != out[x][y]) {
                printf("out(%d, %d) = %d instead of im(%d, %d) = %d\n",
                       x, y, out[x][y], x, y, im(x, y));
                return -1;
            }
        }
    }

    for (int i = 0; i < 200; ++i) {
        delete [] out[i];
    }
    delete [] out;*/

    return 0;
}

int gpu_intermediate_computed_if_param_test(int index) {
    buffer_index = index;

    Func f("f_" + std::to_string(index)), g("g_" + std::to_string(index)), h("h_" + std::to_string(index));
    Var x("x"), y("y");
    Param<int> p;

    g(x, y) = x + y;
    h(x, y) = 10;

    f(x, y) = x + y;
    RDom r1(0, 100, 0, 100);
    r1.where(p > 3);
    f(r1.x, r1.y) += 2*g(r1.x, r1.y);

    RDom r2(0, 100, 0, 100);
    r2.where(p <= 3);
    f(r2.x, r2.y) += h(r2.x, r2.y) + g(r2.x, r2.y);

    f.update(0).specialize(p >= 2).gpu_tile(r1.x, r1.y, 4, 4);
    g.compute_root();
    h.compute_root();
    h.gpu_tile(x, y, 8, 8);

    {
        printf("....Set p to 5, expect g to be computed\n");
        p.set(5);
        run_tracer = false;
        niters_expected = 100*100;
        niters = 0;
        Image<int> im = f.realize(200, 200);
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
                if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                    correct = 3*correct;
                }
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }

    {
        printf("....Set p to 0, expect g to be not computed\n");
        p.set(0);
        run_tracer = false;
        niters_expected = 0;
        niters = 0;
        Image<int> im = f.realize(200, 200);
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = x + y;
                if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                    correct += 10 + correct;
                }
                if (im(x, y) != correct) {
                    printf("im(%d, %d) = %d instead of %d\n",
                           x, y, im(x, y), correct);
                    return -1;
                }
            }
        }
    }
    return 0;
}


int main(int argc, char **argv) {
    /*printf("Running equality inequality bound test\n");
    if (equality_inequality_bound_test(0) != 0) {
        return -1;
    }*/

    /*printf("Running split fuse test\n");
    if (split_fuse_test(1) != 0) {
        return -1;
    }

    printf("Running bound depend on free variable test\n");
    if (free_variable_bound_test(2) != 0) {
        return -1;
    }

    printf("Running function call inside bound test\n");
    if (func_call_inside_bound_test(3) != 0) {
        return -1;
    }

    printf("Running two linear bounds test\n");
    if (two_linear_bounds_test(4) != 0) {
        return -1;
    }

    printf("Running circular bound test\n");
    if (circle_bound_test(5) != 0) {
        return -1;
    }

    printf("Running intermediate only computed if param is bigger than certain value test\n");
    if (intermediate_computed_if_param_test(6) != 0) {
        return -1;
    }

    printf("Running tile intermediate stage depend on output bound test\n");
    if (tile_intermediate_bound_depend_on_output_test(7) != 0) {
        return -1;
    }

    printf("Running intermediate stage depend on output bound\n");
    if (intermediate_bound_depend_on_output_test(8) != 0) {
        return -1;
    }

    printf("Running newton's method test\n");
    if (newton_method_test() != 0) {
        return -1;
    }

    printf("Running initialization on gpu and update on cpu test\n");
    if (init_on_gpu_update_on_cpu_test(9) != 0) {
        return -1;
    }

    printf("Running gpu intermediate only computed if param is bigger than certain value test\n");
    if (gpu_intermediate_computed_if_param_test(11) != 0) {
        return -1;
    }*/

    printf("Running initialization on cpu and update on gpu test\n");
    if (init_on_cpu_update_on_gpu_test(10) != 0) {
        return -1;
    }

    printf("Success!\n");

    return 0;

}
