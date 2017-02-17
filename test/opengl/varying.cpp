#include "Halide.h"
#include <stdio.h>

#include "testing.h"

using namespace Halide;
using namespace Halide::Internal;

// This test exercises several use cases for the GLSL varying attributes
// feature. This feature detects expressions that are linear in terms of the
// loop variables of a .glsl(..) scheduled Func and uses graphics pipeline
// interpolation to evaluate the expressions instead of evaluating them per
// fragment in the Halide generated fragment shader. Common examples are texture
// coordinates interpolated across a Func domain or texture coordinates
// transformed by a matrix and interpolated across the domain. Both cases arise
// when GLSL shaders are ported to Halide.

// This is a mutator that injects code that counts the number of variables
// tagged .varying
#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// This global variable is used to count the number of unique varying attribute
// variables that appear in the lowered Halide IR.
std::set<std::string> varyings;

// This function is a HalideExtern used to add variables to the set. The tests
// below check the total number of unique variables found--not the specific
// names of the variables which are arbitrary.
extern "C" DLLEXPORT const Variable *record_varying(const Variable *op) {
    if (varyings.find(op->name) == varyings.end()) {
        fprintf(stderr, "Found varying attribute: %s\n", op->name.c_str());
        varyings.insert(op->name);
    }
    return op;
}
HalideExtern_1(const Variable *, record_varying, const Variable *);

// This visitor inserts the above function in the IR tree.
class CountVarying : public IRMutator {
    using IRMutator::visit;

    void visit(const Variable *op) {
        IRMutator::visit(op);
        if (ends_with(op->name, ".varying")) {
            expr = record_varying(op);
        }
    }
};

bool perform_test(const char *label, const Target target, Func f, int expected_nvarying, float tol, std::function<float(int x, int y, int c)> expected_val) {
    fprintf(stderr, "%s\n", label);

    Buffer<float> out(8, 8, 3);

    varyings.clear();
    f.add_custom_lowering_pass(new CountVarying);
    f.realize(out, target);

    // Check for the correct number of varying attributes
    if ((int)varyings.size() != expected_nvarying) {
        fprintf(stderr,
                "%s: Error: wrong number of varying attributes: %d should be %d\n",
                label, (int)varyings.size(), expected_nvarying);
        return false;
    }

    // Check for correct result values
    out.copy_to_host();

    if (!Testing::check_result<float>(out, tol, expected_val)) {
        return false;
    }

    fprintf(stderr, "%s Passed!\n", label);
    return true;
}

// This is a simple test case where there are two expressions that are not
// linearly varying in terms of a loop variable and one expression that is.
bool test0(const Target target, Var &x, Var &y, Var &c) {
    float p_value = 8.0f;
    Param<float> p("p");
    p.set(p_value);

    Func f0("f0");
    f0(x, y, c) = select(c == 0, 4.0f,  // Constant term
                         c == 1, p * 10.0f,  // Linear expression not in terms of a loop parameter
                         cast<float>(x) * 100.0f);  // Linear expression in terms of x

    f0.bound(c, 0, 3);
    f0.glsl(x, y, c);
    return perform_test("Test0", target, f0, 2, 0.0f, [&](int x, int y, int c) {
                switch (c) {
                case 0: return 4.0f;
                case 1: return p_value * 10.0f;
                default: return static_cast<float>(x) * 100.0f;
                } });
}

struct CoordXform {
    const float th = 3.141592f / 8.0f;
    const float s_th = sinf(th);
    const float c_th = cosf(th);
    const float m[6] = {
        c_th, -s_th, 0.0f,
        s_th, c_th, 0.0f
    };
    Param<float> m0, m1, m2, m3, m4, m5;
    CoordXform() : m0("m0"), m1("m1"), m2("m2"), m3("m3"), m4("m4"), m5("m5") {
        m0.set(m[0]);
        m1.set(m[1]);
        m2.set(m[2]);
        m3.set(m[3]);
        m4.set(m[4]);
        m5.set(m[5]);
    }
};

// This is a more complicated test case where several expressions are linear
// in all of the loop variables. This is the coordinate transformation case
bool test1(const Target target, Var &x, Var &y, Var &c) {
    struct CoordXform m;
    Func f1("f1");
    f1(x, y, c) = select(c == 0, m.m0 * x + m.m1 * y + m.m2,
                         c == 1, m.m3 * x + m.m4 * y + m.m5,
                         1.0f);

    f1.bound(c, 0, 3);
    f1.glsl(x, y, c);

    return perform_test("Test1", target, f1, 4, 0.000001f, [&](int x, int y, int c) {
                switch (c) {
                    case 0: return m.m[0] * x + m.m[1] * y + m.m[2];
                    case 1: return m.m[3] * x + m.m[4] * y + m.m[5];
                    default: return 1.0f;
                } });
}

// The feature is supposed to find linearly varying sub-expressions as well
// so for example, if the above expressions are wrapped in a non-linear
// function like sqrt, they should still be extracted.
bool test2(const Target target, Var &x, Var &y, Var &c) {
    struct CoordXform m;
    Func f2("f2");
    f2(x, y, c) = select(c == 0, sqrt(m.m0 * x + m.m1 * y + m.m2),
                         c == 1, sqrt(m.m3 * x + m.m4 * y + m.m5),
                         1.0f);
    f2.bound(c, 0, 3);
    f2.glsl(x, y, c);

    return perform_test("Test2", target, f2, 4, 0.000001f, [&](int x, int y, int c) {
                switch (c) {
                    case 0: return sqrtf(m.m[0] * x + m.m[1] * y + m.m[2]);
                    case 1: return sqrtf(m.m[3] * x + m.m[4] * y + m.m[5]);
                    default: return 1.0f;
                } });
}

// This case tests a large expression linearly varying in terms of a loop
// variable
bool test3(const Target target, Var &x, Var &y, Var &c) {
    float p_value = 8.0f;
    Param<float> p("p");
    p.set(p_value);
    Expr foo = p;
    for (int i = 0; i < 10; i++) {
        foo = foo + foo + foo;
    }
    foo = x + foo;

    float foo_value = p_value;
    for (int i = 0; i < 10; i++) {
        foo_value = foo_value + foo_value + foo_value;
    }

    Func f3("f3");
    f3(x, y, c) = select(c == 0, foo,
                         c == 1, 1.0f,
                         2.0f);

    f3.bound(c, 0, 3);
    f3.glsl(x, y, c);

    return perform_test("Test3", target, f3, 2, 0.000001f, [&](int x, int y, int c) {
                switch (c) {
                    case 0: return (float)x + foo_value;
                    case 1: return 1.0f;
                    default: return 2.0f;
                } });
}

int main() {
    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    Var x("x");
    Var y("y");
    Var c("c");

    bool pass = true;
    pass &= test0(target, x, y, c);
    pass &= test1(target, x, y, c);
    pass &= test2(target, x, y, c);
    pass &= test3(target, x, y, c);
    if (!pass) {
        return 1;
    }

    // The test will return early on error.
    fprintf(stderr, "Success!\n");

    // This test may abort with the message "Failed to free device buffer" due
    // to https://github.com/halide/Halide/issues/559
    return 0;
}
