#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
struct TestContextBase : JITUserContext {
    std::string buffer_name;
    bool run_tracer = false;
    int niters = 0;

    explicit TestContextBase(const std::string &buffer_name)
        : buffer_name(buffer_name) {
    }
};

struct IntermediateBoundDependOnOutputContext : TestContextBase {
    IntermediateBoundDependOnOutputContext(const std::string &buffer_name)
        : TestContextBase(buffer_name) {
        handlers.custom_trace = custom_trace;
    }

    static int custom_trace(JITUserContext *ctx, const halide_trace_event_t *e) {
        auto *self = static_cast<IntermediateBoundDependOnOutputContext *>(ctx);
        if (std::string(e->func) == self->buffer_name) {
            if (e->event == halide_trace_produce) {
                self->run_tracer = true;
            } else if (e->event == halide_trace_consume) {
                self->run_tracer = false;
            }

            if (self->run_tracer && e->event == halide_trace_store) {
                EXPECT_LT(e->coordinates[0], e->coordinates[1]);
                EXPECT_GE(e->coordinates[0], 0);
                EXPECT_LE(e->coordinates[0], 199);
                EXPECT_GE(e->coordinates[1], 0);
                EXPECT_LE(e->coordinates[1], 199);
                self->niters++;
            }
        }
        return 0;
    }
};

struct FuncCallBound : TestContextBase {
    FuncCallBound(const std::string &buffer_name)
        : TestContextBase(buffer_name) {
        handlers.custom_trace = custom_trace;
    }

    static int custom_trace(JITUserContext *ctx, const halide_trace_event_t *e) {
        auto *self = static_cast<FuncCallBound *>(ctx);
        if (std::string(e->func) == self->buffer_name) {
            if (e->event == halide_trace_produce) {
                self->run_tracer = true;
            } else if (e->event == halide_trace_consume) {
                self->run_tracer = false;
            }

            if (self->run_tracer && e->event == halide_trace_store) {
                EXPECT_GE(e->coordinates[0], 10);
                EXPECT_LE(e->coordinates[0], 109);
                self->niters++;
            }
        }
        return 0;
    }
};

struct BoxBound : TestContextBase {
    BoxBound(const std::string &buffer_name)
        : TestContextBase(buffer_name) {
        handlers.custom_trace = custom_trace;
    }

    static int custom_trace(JITUserContext *ctx, const halide_trace_event_t *e) {
        auto *self = static_cast<BoxBound *>(ctx);
        if (std::string(e->func) == self->buffer_name) {
            if (e->event == halide_trace_produce) {
                self->run_tracer = true;
            } else if (e->event == halide_trace_consume) {
                self->run_tracer = false;
            }

            if (self->run_tracer && e->event == halide_trace_store) {
                EXPECT_GE(e->coordinates[0], 0);
                EXPECT_LE(e->coordinates[0], 99);
                EXPECT_GE(e->coordinates[1], 0);
                EXPECT_LE(e->coordinates[1], 99);
                self->niters++;
            }
        }
        return 0;
    }
};
}  // namespace

TEST(ReductionNonRectangularTest, EqualityInequalityBound) {
    Func f;
    Var x("x"), y("y");
    f(x, y) = x + y;

    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    r.where(!(r.x != 10));
    f(r.x, r.y) += 1;

    Buffer<int> im = f.realize({200, 200});
    for (int yy = 0; yy < im.height(); yy++) {
        for (int xx = 0; xx < im.width(); xx++) {
            int correct = xx + yy;
            if (xx == 10 && 0 <= yy && yy <= 99) {
                correct += xx < yy ? 1 : 0;
            }
            EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }
}

TEST(ReductionNonRectangularTest, SplitFuse) {
    Func f;
    Var x("x"), y("y");
    f(x, y) = x + y;

    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    f(r.x, r.y) += 1;

    RVar rx_outer, rx_inner, r_fused;
    f.update().reorder(r.y, r.x);
    f.update().split(r.x, rx_outer, rx_inner, 4);
    f.update().fuse(rx_inner, r.y, r_fused);

    Buffer<int> im = f.realize({200, 200});
    for (int yy = 0; yy < im.height(); yy++) {
        for (int xx = 0; xx < im.width(); xx++) {
            int correct = xx + yy;
            if (0 <= xx && xx <= 99 && 0 <= yy && yy <= 99) {
                correct += xx < yy ? 1 : 0;
            }
            EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }
}

TEST(ReductionNonRectangularTest, FreeVariableBound) {
    Func f;
    Var x("x"), y("y"), z("z");
    f(x, y, z) = x + y + z;

    RDom r(0, 100, 0, 100, "r");
    r.where(r.x < r.y + z);
    f(r.x, r.y, z) += 1;

    Buffer<int> im = f.realize({200, 200, 200});
    for (int zz = 0; zz < im.channels(); zz++) {
        for (int yy = 0; yy < im.height(); yy++) {
            for (int xx = 0; xx < im.width(); xx++) {
                int correct = xx + yy + zz;
                if (0 <= xx && xx <= 99 && 0 <= yy && yy <= 99) {
                    correct += xx < yy + zz ? 1 : 0;
                }
                EXPECT_EQ(im(xx, yy, zz), correct)
                    << "x = " << xx << ", y = " << yy << ", z = " << zz;
            }
        }
    }
}

TEST(ReductionNonRectangularTest, FuncCallInsideBound) {
    Func f, g;
    Var x("x"), y("y");

    g(x) = x;

    f(x, y) = x + y;

    RDom r(0, 100, 0, 100, "r");
    r.where(r.x < g(r.y + 10));
    f(r.x, r.y) += 1;

    // Expect g to be computed over x=[10, 109].
    g.compute_root();
    g.trace_stores();
    g.trace_realizations();

    FuncCallBound ctx(g.name());
    Buffer<int> im = f.realize(&ctx, {200, 200});

    for (int yy = 0; yy < im.height(); yy++) {
        for (int xx = 0; xx < im.width(); xx++) {
            int correct = xx + yy;
            if (0 <= xx && xx <= 99 && 0 <= yy && yy <= 99) {
                correct += xx < yy + 10 ? 1 : 0;
            }
            EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }

    EXPECT_EQ(ctx.niters, 100);
}

TEST(ReductionNonRectangularTest, FuncCallInsideBoundInline) {
    Func f, g, h;
    Var x("x"), y("y");

    g(x) = x;
    h(x) = 2 * x;

    f(x, y) = x + y;

    RDom r(0, 100, 0, 100, "r");
    r.where(r.x < g(r.y) + h(r.x));
    f(r.x, r.y) += 1;

    Buffer<int> im = f.realize({200, 200});

    for (int yy = 0; yy < im.height(); yy++) {
        for (int xx = 0; xx < im.width(); xx++) {
            int correct = xx + yy;
            if (0 <= xx && xx <= 99 && 0 <= yy && yy <= 99) {
                correct += xx < yy + 2 * xx ? 1 : 0;
            }
            EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }
}

TEST(ReductionNonRectangularTest, TwoLinearBounds) {
    Func f, g;
    Var x("x"), y("y");

    g(x, y) = x + y;

    f(x, y) = x + y;
    RDom r(0, 100, 0, 100);
    r.where(2 * r.x + 30 < r.y);
    r.where(r.y >= 100 - r.x);
    f(r.x, r.y) += 2 * g(r.x, r.y);

    // Expect g to be computed over x=[0,99] and y=[1,99].
    g.compute_root();
    g.trace_stores();
    g.trace_realizations();

    // The first condition means r.x. can be at most 34 (2*34 + 30 =
    // 98 < 99).  The second condition means r.x must be at least 1,
    // so there are 34 legal values for r.x.  The second condition
    // also means that r.y is at least 100 - 34 and at most 99, so
    // there are also 34 legal values of it. We only actually iterate
    // over a triangle within this box, but Halide takes bounding
    // boxes for bounds relationships.
    BoxBound ctx(g.name());
    Buffer<int> im = f.realize(&ctx, {200, 200});
    for (int yy = 0; yy < im.height(); yy++) {
        for (int xx = 0; xx < im.width(); xx++) {
            int correct = xx + yy;
            if (0 <= xx && xx <= 99 && 0 <= yy && yy <= 99) {
                correct = 2 * xx + 30 < yy && yy >= 100 - xx ? 3 * correct : correct;
            }
            EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }

    EXPECT_EQ(ctx.niters, 34 * 34);
}

TEST(ReductionNonRectangularTest, CircleBound) {
    Func f, g;
    Var x("x"), y("y");
    g(x, y) = x;
    f(x, y) = x + y;

    // Iterate over circle with radius of 10
    RDom r(0, 100, 0, 100);
    r.where(r.x * r.x + r.y * r.y <= 100);
    f(r.x, r.y) += g(r.x, r.y);

    // Expect g to be still computed over x=[0,99] and y=[0,99]. The predicate
    // guard for the non-linear term will be left as is in the inner loop of f,
    // i.e. f loop will still iterate over x=[0,99] and y=[0,99].
    g.compute_at(f, r.y);
    g.trace_stores();
    g.trace_realizations();

    BoxBound ctx(g.name());
    Buffer<int> im = f.realize(&ctx, {200, 200});
    for (int yy = 0; yy < im.height(); yy++) {
        for (int xx = 0; xx < im.width(); xx++) {
            int correct = xx + yy;
            if (0 <= xx && xx <= 99 && 0 <= yy && yy <= 99) {
                correct += xx * xx + yy * yy <= 100 ? xx : 0;
            }
            EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }

    EXPECT_EQ(ctx.niters, 100 * 100);
}

TEST(ReductionNonRectangularTest, IntermediateComputedIfParam) {
    Func f, g;
    Var x("x"), y("y");
    Param<int> p;

    g(x, y) = x + y;

    f(x, y) = x + y;
    RDom r(0, 100, 0, 100);
    r.where(p > 3);
    f(r.x, r.y) += 2 * g(r.x, r.y);

    // Expect g to be only computed over x=[0,99] and y=[0,99] if param is bigger
    // than 3.
    g.compute_root();
    g.trace_stores();
    g.trace_realizations();

    {
        SCOPED_TRACE("Set p to 5, expect g to be computed");
        p.set(5);
        BoxBound ctx(g.name());
        Buffer<int> im = f.realize(&ctx, {200, 200});
        for (int yy = 0; yy < im.height(); yy++) {
            for (int xx = 0; xx < im.width(); xx++) {
                int correct = xx + yy;
                if (0 <= xx && xx <= 99 && 0 <= yy && yy <= 99) {
                    correct = 3 * correct;
                }
                EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
            }
        }
        EXPECT_EQ(ctx.niters, 100 * 100);
    }

    {
        SCOPED_TRACE("Set p to 0, expect g to be not computed");
        p.set(0);
        BoxBound ctx(g.name());
        Buffer<int> im = f.realize(&ctx, {200, 200});
        for (int yy = 0; yy < im.height(); yy++) {
            for (int xx = 0; xx < im.width(); xx++) {
                int correct = xx + yy;
                EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
            }
        }
        EXPECT_EQ(ctx.niters, 0);
    }
}

TEST(ReductionNonRectangularTest, IntermediateBoundDependOnOutput) {
    Func f, g;
    Var x("x"), y("y");

    g(x, y) = x;
    f(x, y) = x + y;

    RDom r(0, 200, 0, 200);
    r.where(r.x < r.y);
    f(r.x, r.y) = g(r.x, r.y);

    // Expect bound of g on r.x to be directly dependent on the simplified
    // bound of f on r.x, which should have been r.x = [0, r.y) in this case
    g.compute_at(f, r.y);
    g.trace_stores();
    g.trace_realizations();

    IntermediateBoundDependOnOutputContext ctx(g.name());
    Buffer<int> im = f.realize(&ctx, {200, 200});

    for (int yy = 0; yy < im.height(); yy++) {
        for (int xx = 0; xx < im.width(); xx++) {
            int correct = xx + yy;
            if (0 <= xx && xx <= 199 && 0 <= yy && yy <= 199) {
                if (xx < yy) {
                    correct = xx;
                }
            }
            EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }

    EXPECT_EQ(ctx.niters, 200 * 199 / 2);
}

TEST(ReductionNonRectangularTest, TileIntermediateBoundDependOnOutput) {
    Func f, g;
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
    g.trace_stores();
    g.trace_realizations();

    IntermediateBoundDependOnOutputContext ctx(g.name());
    Buffer<int> im = f.realize(&ctx, {200, 200});

    for (int yy = 0; yy < im.height(); yy++) {
        for (int xx = 0; xx < im.width(); xx++) {
            int correct = xx + yy;
            if (0 <= xx && xx <= 199 && 0 <= yy && yy <= 199) {
                correct += xx < yy ? xx : 0;
            }
            EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }

    EXPECT_EQ(ctx.niters, 200 * 199 / 2);
}

TEST(ReductionNonRectangularTest, SelfReferenceBound) {
    Func f, g;
    Var x("x"), y("y");
    f(x, y) = x + y;
    g(x, y) = 10;

    RDom r1(0, 100, 0, 100, "r1");
    r1.where(f(r1.x, r1.y) >= 40);
    r1.where(f(r1.x, r1.y) != 50);
    f(r1.x, r1.y) += 1;
    f.compute_root();

    RDom r2(0, 50, 0, 50, "r2");
    r2.where(f(r2.x, r2.y) < 30);
    g(r2.x, r2.y) += f(r2.x, r2.y);

    Buffer<int> im1 = f.realize({200, 200});
    for (int yy = 0; yy < im1.height(); yy++) {
        for (int xx = 0; xx < im1.width(); xx++) {
            int correct = xx + yy;
            if (0 <= xx && xx <= 99 && 0 <= yy && yy <= 99) {
                correct += correct >= 40 && correct != 50 ? 1 : 0;
            }
            EXPECT_EQ(im1(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }

    Buffer<int> im2 = g.realize({200, 200});
    for (int yy = 0; yy < im2.height(); yy++) {
        for (int xx = 0; xx < im2.width(); xx++) {
            int correct = 10;
            if (0 <= xx && xx <= 49 && 0 <= yy && yy <= 49) {
                correct += im1(xx, yy) < 30 ? im1(xx, yy) : 0;
            }
            EXPECT_EQ(im2(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }
}

TEST(ReductionNonRectangularTest, RandomFloatBound) {
    Func f;
    Var x("x"), y("y");

    Expr e1 = random_float() < 0.5f;
    f(x, y) = Tuple(e1, x + y);

    RDom r(0, 100, 0, 100);
    r.where(f(r.x, r.y)[0]);
    f(r.x, r.y) = Tuple(f(r.x, r.y)[0], f(r.x, r.y)[1] + 10);

    Realization res = f.realize({200, 200});
    ASSERT_EQ(res.size(), 2);
    Buffer<bool> im0 = res[0];
    Buffer<int> im1 = res[1];

    int n_true = 0;
    for (int yy = 0; yy < im1.height(); yy++) {
        for (int xx = 0; xx < im1.width(); xx++) {
            n_true += im0(xx, yy);
            int correct = xx + yy;
            if (0 <= xx && xx <= 99 && 0 <= yy && yy <= 99) {
                correct += im0(xx, yy) ? 10 : 0;
            }
            EXPECT_EQ(im1(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }
    EXPECT_GE(n_true, 19000);
    EXPECT_LE(n_true, 21000);
}

TEST(ReductionNonRectangularTest, NewtonMethod) {
    Func inverse;
    Var x;
    // Negating the bits of a float is a piecewise linear approximation to inverting it
    inverse(x) = {-0.25f * reinterpret<float>(~reinterpret<uint32_t>(cast<float>(x + 1))), 0};
    constexpr int kMaxIters = 10;
    RDom r(0, kMaxIters);
    Expr not_converged = abs(inverse(x)[0] * (x + 1) - 1) > 0.001f;
    r.where(not_converged);

    // Compute the inverse of x using Newton's method, and count the
    // number of iterations required to reach convergence
    inverse(x) = {inverse(x)[0] * (2 - (x + 1) * inverse(x)[0]),
                  r + 1};

    Realization result = inverse.realize({128});
    Buffer<float> r0 = result[0];
    Buffer<int> r1 = result[1];
    for (int i = 0; i < r0.width(); i++) {
        float xx = i + 1;
        int num_iters = r1(i);
        EXPECT_NE(num_iters, kMaxIters) << "Failed to converge!";
        EXPECT_NEAR(xx * r0(i), 1.0f, 0.001)
            << "x = " << xx << ", r0(i) = " << r0(i)
            << ", num_iters = " << num_iters << ", i = " << i;
    }
}

TEST(ReductionNonRectangularTest, InitOnGpuUpdateOnCpu) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled";
    }

    Func f;
    Var x("x"), y("y");
    f(x, y) = x + y;

    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    r.where(!(r.x != 10));
    f(r.x, r.y) += 3;

    Var xi("xi"), yi("yi");
    f.gpu_tile(x, y, xi, yi, 4, 4);

    Buffer<int> im = f.realize({200, 200});
    for (int yy = 0; yy < im.height(); yy++) {
        for (int xx = 0; xx < im.width(); xx++) {
            int correct = xx + yy;
            if (xx == 10 && 0 <= yy && yy <= 99) {
                correct += xx < yy ? 3 : 0;
            }
            EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }
}

TEST(ReductionNonRectangularTest, InitOnCpuUpdateOnGpu) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled";
    }

    Func f;
    Var x("x"), y("y");
    f(x, y) = x + y;

    RDom r(0, 100, 0, 100);
    r.where(!(r.x != 10));
    r.where(r.x < r.y);
    f(r.x, r.y) += 3;

    RVar rxi("rxi"), ryi("ryi");
    f.update(0).gpu_tile(r.x, r.y, r.x, r.y, rxi, ryi, 4, 4);

    Buffer<int> im = f.realize({200, 200});
    for (int yy = 0; yy < im.height(); yy++) {
        for (int xx = 0; xx < im.width(); xx++) {
            int correct = xx + yy;
            if (xx == 10 && 0 <= yy && yy <= 99) {
                correct += xx < yy ? 3 : 0;
            }
            EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }
}

TEST(ReductionNonRectangularTest, GpuIntermediateComputedIfParam) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled";
    }

    Func f, g, h;
    Var x("x"), y("y");
    Param<int> p;

    g(x, y) = x + y;
    h(x, y) = 10;

    f(x, y) = x + y;
    RDom r1(0, 100, 0, 100);
    r1.where(p > 3);
    f(r1.x, r1.y) += 2 * g(r1.x, r1.y);

    RDom r2(0, 100, 0, 100);
    r2.where(p <= 3);
    f(r2.x, r2.y) += h(r2.x, r2.y) + g(r2.x, r2.y);

    RVar r1xi("r1xi"), r1yi("r1yi");
    f.update(0).specialize(p >= 2).gpu_tile(r1.x, r1.y, r1xi, r1yi, 4, 4);
    g.compute_root();
    h.compute_root();
    Var xi("xi"), yi("yi");
    h.gpu_tile(x, y, xi, yi, 8, 8);

    {
        SCOPED_TRACE("Set p to 5, expect g to be computed");
        p.set(5);
        Buffer<int> im = f.realize({200, 200});
        for (int yy = 0; yy < im.height(); yy++) {
            for (int xx = 0; xx < im.width(); xx++) {
                int correct = xx + yy;
                if (0 <= xx && xx <= 99 && 0 <= yy && yy <= 99) {
                    correct = 3 * correct;
                }
                EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
            }
        }
    }

    {
        SCOPED_TRACE("Set p to 0, expect g to be not computed");
        p.set(0);
        Buffer<int> im = f.realize({200, 200});
        for (int yy = 0; yy < im.height(); yy++) {
            for (int xx = 0; xx < im.width(); xx++) {
                int correct = xx + yy;
                if (0 <= xx && xx <= 99 && 0 <= yy && yy <= 99) {
                    correct += 10 + correct;
                }
                EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
            }
        }
    }
}

TEST(ReductionNonRectangularTest, VectorizePredicatedRvar) {
    Func f("f");
    Var x("x"), y("y");
    f(x, y) = 0;

    Expr w = f.output_buffer().width() / 2 * 2;
    Expr h = f.output_buffer().height() / 2 * 2;

    RDom r(1, w - 2, 1, h - 2);
    r.where((r.x + r.y) % 2 == 0);

    f(r.x, r.y) += 10;

    f.update(0).unroll(r.x, 2).allow_race_conditions().vectorize(r.x, 8);

    Buffer<int> im = f.realize({200, 200});
    for (int yy = 0; yy < im.height(); yy++) {
        for (int xx = 0; xx < im.width(); xx++) {
            int correct = 0;
            if (1 <= xx && xx < im.width() - 1 && 1 <= yy && yy < im.height() - 1 &&
                (xx + yy) % 2 == 0) {
                correct += 10;
            }
            EXPECT_EQ(im(xx, yy), correct) << "x = " << xx << ", y = " << yy;
        }
    }
}
