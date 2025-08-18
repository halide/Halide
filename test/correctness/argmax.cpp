#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ArgmaxTest, SingleDimensional) {
    // A single-dimensional argmax.
    Func f, arg_max_f;
    Var x;

    f(x) = x * (100 - x);

    RDom r(0, 100);
    arg_max_f() = 0;
    // The clamp is necessary, because otherwise we'll be indexing f
    // at a location determined by a load from a Halide image, and we
    // don't infer anything about how large that could be.
    Expr best_so_far = f(clamp(arg_max_f(), 0, 100));
    arg_max_f() = select(f(r) > best_so_far, r, arg_max_f());

    int result_f = evaluate_may_gpu<int>(arg_max_f());

    EXPECT_EQ(result_f, 50) << "Arg max of f is " << result_f << ", but should have been 50";
}

TEST(ArgmaxTest, MultiDimensional) {
    // Try a multi-dimensional argmax.
    Func g, arg_max_g;
    Var x, y;
    RDom r(0, 100, 0, 100);
    g(x, y) = x * (100 - x) + y * (80 - y);
    g.compute_root();

    arg_max_g() = Tuple(0, 0, g(0, 0));
    arg_max_g() = select(g(r.x, r.y) > arg_max_g()[2],
                         Tuple(r.x, r.y, g(r.x, r.y)),
                         arg_max_g());

    int best_x, best_y, best_val;
    evaluate_may_gpu(arg_max_g(), &best_x, &best_y, &best_val);

    EXPECT_EQ(best_val, 4100) << "Arg max of g is " << best_val << ", but should have been 4100";
    EXPECT_EQ(best_x, 50) << "Arg max x of g is " << best_x << ", but should have been 50";
    EXPECT_EQ(best_y, 40) << "Arg max y of g is " << best_y << ", but should have been 40";
}

TEST(ArgmaxTest, InlineArgmaxArgmin) {
    // Try some inline argmaxs
    Func g;
    Var x, y;
    RDom r(0, 100, 0, 100);
    g(x, y) = x * (100 - x) + y * (80 - y);
    g.compute_root();

    int best_x, best_y, best_val;
    evaluate_may_gpu(argmax(g(r.x, r.y)), &best_x, &best_y, &best_val);

    EXPECT_EQ(best_x, 50) << "Inline arg max x of g is " << best_x << ", but should have been 50";
    EXPECT_EQ(best_y, 40) << "Inline arg max y of g is " << best_y << ", but should have been 40";
    EXPECT_EQ(best_val, 4100) << "Inline arg max val of g is " << best_val << ", but should have been 4100";

    evaluate_may_gpu(argmin(g(r.x, r.y)), &best_x, &best_y, &best_val);

    EXPECT_EQ(best_x, 0) << "Inline arg min x of g is " << best_x << ", but should have been 0";
    EXPECT_EQ(best_y, 99) << "Inline arg min y of g is " << best_y << ", but should have been 99";
    EXPECT_EQ(best_val, -1881) << "Inline arg min val of g is " << best_val << ", but should have been -1881";
}

TEST(ArgmaxTest, InPlaceArgmaxVariousStarts) {
    // Try an in place argmax, using an elements at various places in
    // the sequence as the initial guess.  This tests some edge cases
    // for the atomicity of provides.
    int starts[] = {
        -1,
        0,
        1,
        5,
        99,
        100,
        101,
    };
    
    for (size_t i = 0; i < sizeof(starts) / sizeof(starts[0]); i++) {
        int init = starts[i];
        Func h;
        Var x;
        RDom r(0, 100);
        h(x) = Tuple(x * (100 - x), x);
        h(init) = select(h(init)[0] >= h(r)[0], Tuple(h(init)), Tuple(h(r)));

        Func arg_max_h;
        arg_max_h() = h(init);

        int best_val, best_x;
        evaluate_may_gpu(arg_max_h(), &best_val, &best_x);

        EXPECT_EQ(best_val, 2500) << "Arg max val of h with init=" << init << " is " << best_val << ", but should have been 2500";
        EXPECT_EQ(best_x, 50) << "Arg max x of h with init=" << init << " is " << best_x << ", but should have been 50";
    }
}
