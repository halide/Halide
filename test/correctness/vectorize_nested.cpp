#include "Halide.h"
#include "check_call_graphs.h"

using namespace Halide;

int vectorize_2d_round_up() {
    const int width = 32;
    const int height = 24;

    Func f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f(x, y) = 3 * x + y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::RoundUp)
        .vectorize(xi)
        .vectorize(yi);

    Buffer<int> result = f.realize({width, height});

    auto cmp_func = [](int x, int y) {
        return 3 * x + y;
    };
    if (check_image(result, cmp_func)) {
        return 1;
    }

    return 0;
}

int vectorize_2d_guard_with_if_and_predicate() {
    for (TailStrategy tail_strategy : {TailStrategy::GuardWithIf, TailStrategy::Predicate}) {
        const int width = 33;
        const int height = 22;

        Func f("f");
        Var x("x"), y("y"), xi("xi"), yi("yi");

        f(x, y) = 3 * x + y;

        f.compute_root()
            .tile(x, y, x, y, xi, yi, 8, 4, tail_strategy)
            .vectorize(xi)
            .vectorize(yi);

        Buffer<int> result = f.realize({width, height});

        auto cmp_func = [](int x, int y) {
            return 3 * x + y;
        };
        if (check_image(result, cmp_func)) {
            return 1;
        }
    }
    return 0;
}

int vectorize_2d_inlined_with_update() {
    const int width = 33;
    const int height = 22;

    Func f, inlined;
    Var x("x"), y("y"), xi("xi"), yi("yi");
    RDom r(0, 10, "r");
    inlined(x) = x;
    inlined(x) += r;
    f(x, y) = inlined(x) + 2 * y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::GuardWithIf)
        .vectorize(xi)
        .vectorize(yi);

    Buffer<int> result = f.realize({width, height});

    auto cmp_func = [](int x, int y) {
        return x + 2 * y + 45;
    };
    if (check_image(result, cmp_func)) {
        return 1;
    }

    return 0;
}

int vectorize_2d_with_inner_for() {
    const int width = 33;
    const int height = 22;

    Func f;
    Var x("x"), y("y"), c("c"), xi("xi"), yi("yi");
    f(x, y, c) = 3 * x + y + 7 * c;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 8, 4, TailStrategy::GuardWithIf)
        .reorder(c, xi, yi, x, y)
        .vectorize(xi)
        .vectorize(yi);

    Buffer<int> result = f.realize({width, height, 3});

    auto cmp_func = [](int x, int y, int c) {
        return 3 * x + y + 7 * c;
    };
    if (check_image(result, cmp_func)) {
        return 1;
    }

    return 0;
}

int vectorize_2d_with_compute_at_vectorized() {
    const int width = 16;
    const int height = 16;

    Func f("f"), g("g");
    Var x("x"), y("y");
    f(x, y) = 3 * x + y;
    g(x, y) = f(x, y) + f(x + 1, y);

    Var xi("xi");
    g.split(x, x, xi, 8).vectorize(xi);
    f.compute_at(g, xi).vectorize(x);

    Buffer<int> result = g.realize({width, height});

    auto cmp_func = [](int x, int y) {
        return 6 * x + 3 + 2 * y;
    };
    if (check_image(result, cmp_func)) {
        return 1;
    }

    return 0;
}

int vectorize_2d_with_compute_at() {
    const int width = 35;
    const int height = 17;

    Func f("f"), g("g");
    Var x("x"), y("y");
    f(x, y) = 3 * x + y;
    g(x, y) = f(x, y) + f(x + 1, y);

    Var xi("xi"), xii("xii");
    g.split(x, x, xi, 8, TailStrategy::GuardWithIf)
        .split(xi, xi, xii, 2, TailStrategy::GuardWithIf)
        .vectorize(xi)
        .vectorize(xii);
    f.compute_at(g, xii).vectorize(x);

    Buffer<int> result = g.realize({width, height});

    auto cmp_func = [](int x, int y) {
        return 6 * x + 3 + 2 * y;
    };
    if (check_image(result, cmp_func)) {
        return 1;
    }

    return 0;
}

int vectorize_all_d() {
    const int width = 12;
    const int height = 10;

    Func f("f");
    Var x("x"), y("y"), xi("xi"), yi("yi");

    f(x, y) = 3 * x + y;

    f.compute_root()
        .tile(x, y, x, y, xi, yi, 4, 2, TailStrategy::GuardWithIf)
        .vectorize(x)
        .vectorize(y)
        .vectorize(xi)
        .vectorize(yi);

    f.bound(x, 0, width).bound(y, 0, height);
    Buffer<int> result = f.realize({width, height});

    auto cmp_func = [](int x, int y) {
        return 3 * x + y;
    };
    if (check_image(result, cmp_func)) {
        return 1;
    }

    return 0;
}

int vectorize_inner_of_scalarization() {
    ImageParam in(UInt(8), 2);

    Var x("x_inner"), y("y_inner");

    Func out;
    out(x, y) = in(x, y);

    Var xo("xo"), yo("yo");
    out.split(x, xo, x, 8, TailStrategy::RoundUp)
        .split(y, yo, y, 8, TailStrategy::GuardWithIf)
        .vectorize(x)
        .vectorize(y);

    // We are looking for a specific loop, which shouldn't have been scalarized.
    class CheckForScalarizedLoop : public Internal::IRMutator {
        using IRMutator::visit;

        Internal::Stmt visit(const Internal::For *op) override {
            if (Internal::ends_with(op->name, ".x_inner")) {
                *x_loop_found = true;
            }

            if (Internal::ends_with(op->name, ".y_inner")) {
                *y_loop_found = true;
            }

            return IRMutator::visit(op);
        }

    public:
        explicit CheckForScalarizedLoop(bool *fx, bool *fy)
            : x_loop_found(fx), y_loop_found(fy) {
        }

        bool *x_loop_found = nullptr;
        bool *y_loop_found = nullptr;
    };

    bool is_x_loop_found = false;
    bool is_y_loop_found = false;

    out.add_custom_lowering_pass(new CheckForScalarizedLoop(&is_x_loop_found, &is_y_loop_found));

    out.compile_jit();

    if (is_x_loop_found) {
        std::cerr << "Found scalarized loop for " << x << "\n";

        return 1;
    }

    if (!is_y_loop_found) {
        std::cerr << "Expected to find scalarized loop for " << y << "\n";

        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if (vectorize_2d_round_up()) {
        printf("vectorize_2d_round_up failed\n");
        return 1;
    }

    if (vectorize_2d_guard_with_if_and_predicate()) {
        printf("vectorize_2d_guard_with_if failed\n");
        return 1;
    }

    if (vectorize_2d_inlined_with_update()) {
        printf("vectorize_2d_inlined_with_update failed\n");
        return 1;
    }

    if (vectorize_2d_with_inner_for()) {
        printf("vectorize_2d_with_inner_for failed\n");
        return 1;
    }

    if (vectorize_2d_with_compute_at()) {
        printf("vectorize_2d_with_compute_at failed\n");
        return 1;
    }

    if (vectorize_2d_with_compute_at_vectorized()) {
        printf("vectorize_2d_with_compute_at_vectorized failed\n");
        return 1;
    }

    if (vectorize_all_d()) {
        printf("vectorize_all_d failed\n");
        return 1;
    }

    if (vectorize_inner_of_scalarization()) {
        printf("vectorize_inner_of_scalarization failed\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
