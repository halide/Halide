#include "Halide.h"

#include <iostream>
#include <map>
#include <random>

using namespace Halide;
using namespace Halide::Internal;

constexpr int all = -1, success = 0, bad_output = 1, failed_vectorization = 2;

int test(int which_case = all) {

    constexpr int vec = 8;

    Func g{"g"};
    Var x{"x"}, y{"y"}, z{"z"};
    RDom r(0, vec);

    ImageParam input(Int(32), 3);
    Buffer<int> input_buf(vec, vec, vec);
    input_buf.for_each_element([&](int x, int y, int z) {
        input_buf(x, y, z) = x + y * 10 + z * 100;
    });
    input.set(input_buf);

    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 3; j++) {
            int idx = i * 3 + j;
            if (which_case == all || which_case == idx) {
                switch (j) {
                case 0:
                    g(x, y) += input(x, y, r);
                    break;
                case 1:
                    g(x, y) += input(x, r, y);
                    break;
                case 2:
                    g(x, y) += input(r, x, y);
                    break;
                }
            }
        }
    }

    std::vector<VarOrRVar> orders[6] =
        {{x, y, r},
         {x, r, y},
         {r, x, y},
         {y, x, r},
         {y, r, x},
         {r, y, x}};

    Buffer<int> correct = g.realize({vec, vec});

    g.bound(x, 0, vec)
        .bound(y, 0, vec)
        .vectorize(x)
        .vectorize(y);

    int u = 0;
    for (int i = 0; i < 6; i++) {
        for (int j = 0; j < 3; j++) {
            int idx = i * 3 + j;
            if (which_case == all || idx == which_case) {
                g.update(u++)
                    .vectorize(x)
                    .vectorize(y)
                    .atomic()
                    .vectorize(r)
                    .reorder(orders[i]);
            }
        }
    }

    // We need to know the stride on the output buffer is such that rows don't
    // alias each other. That would be UB, but not UB that the vectorizer knows
    // how to exploit. It's more interesting if the stride is not vec - it's a genuine 2D store.
    // g.output_buffer().dim(1).set_stride(vec + 7);

    int for_loops = 0, gathers = 0;
    auto checker = LambdaMutator{
        [&](auto *self, const For *op) {
            for_loops++;
            return self->visit_base(op);
        },
        [&](auto *self, const Load *op) {
            const Ramp *r = op->index.as<Ramp>();
            gathers += !r || !is_const_one(r->stride);
            return self->visit_base(op);
        }};

    g.add_custom_lowering_pass(&checker, nullptr);

    Buffer<int> out = g.realize({vec, vec});

    for (int y = 0; y < vec; y++) {
        for (int x = 0; x < vec; x++) {
            if (out(x, y) != correct(x, y)) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), correct(x, y));
                return bad_output;
            }
        }
    }

    if (which_case == all && for_loops) {
        printf("Atomic vectorization failed. Lowered code contained %d for loops\n", for_loops);
        return failed_vectorization;
    }

    if (which_case == all && gathers) {
        printf("Atomic vectorization produced %d vector gathers\n", gathers);
        return failed_vectorization;
    }

    if (which_case != all) {
        g.compile_to_lowered_stmt(std::string("test_") + std::to_string(which_case) + ".stmt", {input}, StmtOutputFormat::Text, Target{"host-no_asserts-no_runtime-no_bounds_query"});
        g.compile_to_assembly(std::string("test_") + std::to_string(which_case) + ".s", {input}, Target{"host-no_asserts-no_runtime-no_bounds_query"});
    }

    return success;
}

// Generate a random quasi-affine expression in the given RVars: an affine
// combination of terms where each term is one of v, v/k, v%k, or recursively
// one of those of a nested term. All divisors are required to divide the
// corresponding RVar's extent, so the expression is representable as a
// multiramp of the RDom. Coefficients and constant terms are small ints.
struct RVarInfo {
    RVar var;
    int extent;
};

Expr random_term(std::mt19937 &rng, const RDom &rdom) {
    const RVar &chosen = rdom[rng() % rdom.dimensions()];
    int extent = *as_const_int(chosen.extent());
    int op = (int)(rng() % 3);  // 0: leaf, 1: /k, 2: %k
    if (op == 0 || extent <= 1) {
        return chosen;
    }
    std::vector<int> divisors;
    for (int d = 2; d <= extent; d++) {
        if (extent % d == 0) {
            divisors.push_back(d);
        }
    }
    if (divisors.empty()) {
        return chosen;
    }
    int k = divisors[rng() % divisors.size()];
    return (op == 1) ? chosen / k : chosen % k;
}

Expr random_qa(std::mt19937 &rng, const RDom &rdom) {
    int n_terms = 1 + (int)(rng() % 3);
    Expr e;
    for (int i = 0; i < n_terms; i++) {
        int coeff = (int)(rng() % 5) - 2;  // -2..2
        if (coeff == 0) continue;
        Expr term = random_term(rng, rdom);
        Expr part = (coeff == 1) ? term : coeff * term;
        e = e.defined() ? e + part : part;
    }
    if (!e.defined()) e = 0;
    int c0 = (int)(rng() % 7) - 3;  // -3..3
    if (c0 != 0) e = e + c0;
    return e;
}

int test_random() {
    std::mt19937 rng(0);
    RDom r(0, 8, 0, 9, 0, 6);

    // Generous symmetric range for both the input and the output. Halide's
    // bounds inference figures out what it actually needs within this.
    constexpr int half = 256;
    constexpr int range = 2 * half;

    Buffer<int> input_buf(range);
    input_buf.set_min(-half);

    constexpr int num_cases = 1000;
    int tried = 0;
    while (tried < num_cases) {
        Expr A = random_qa(rng, r);
        Expr B = random_qa(rng, r);
        int t = tried++;

        for (int i = 0; i < range; i++) {
            input_buf(i - half) = (i * 31 + t * 7) & 0xff;
        }
        ImageParam input(Int(32), 1);
        input.set(input_buf);

        auto build = [&](bool vectorized) {
            Func f{"f_rand"};
            Var x{"x"};
            f(x) = 0;
            f(A) += input(B) + 0 * r.x;  // Force a dependence on the RDom
            if (vectorized) {
                f.update().atomic().vectorize(r.x).vectorize(r.y).vectorize(r.z);
            }
            return f;
        };

        auto realize = [&](Func f) {
            Buffer<int> buf(range);
            buf.set_min(-half);
            f.realize(buf);
            return buf;
        };

        Buffer<int> correct = realize(build(false));
        Buffer<int> out = realize(build(true));

        for (int i = -half; i < half; i++) {
            if (out(i) != correct(i)) {
                std::cout << "Random case " << t << " failed:\n"
                          << "  A = " << A << "\n"
                          << "  B = " << B << "\n"
                          << "  out(" << i << ") = " << out(i)
                          << " instead of " << correct(i) << "\n";
                return bad_output;
            }
        }
    }
    return success;
}

int main(int argc, char **argv) {

    int result = test(all);

    if (result == bad_output) {
        for (int i = 0; i < 18; i++) {
            if (test(i) != success) {
                printf("Test case %d failed\n", i);
                return result;
            }
        }
    } else if (result != success) {
        return result;
    }

    int rand_result = test_random();
    if (rand_result != success) {
        return rand_result;
    }

    printf("Success!\n");
    return 0;
}
