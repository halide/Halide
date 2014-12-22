#include <Halide.h>
#include <stdio.h>
#include <stdlib.h>

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
extern "C" DLLEXPORT const Variable* record_varying(const Variable *op) {
    if (varyings.find(op->name) == varyings.end()) {
        fprintf(stderr, "Found varying attribute: %s\n", op->name.c_str());
        varyings.insert(op->name);
    }
    return op;
}
HalideExtern_1(const Variable *, record_varying, const Variable *);

// This visitor inserts the above function in the IR tree.
class CountVarying : public IRMutator {
    void visit(const Variable *op) {
        IRMutator::visit(op);
        if (ends_with(op->name, ".varying")) {
            expr = record_varying(op);
        }
    }
};

int main() {

    // This test must be run with an OpenGL target
    const Target &target = get_jit_target_from_environment();
    if (!target.has_feature(Target::OpenGL))  {
        fprintf(stderr,"ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
        return 1;
    }

    Var x("x");
    Var y("y");
    Var c("c");

    // This is a simple test case where there are two expressions that are not
    // linearly varying in terms of a loop variable and one expression that is.
    fprintf(stderr, "Test f0\n");

    float p_value = 8.0f;
    Param<float> p("p"); p.set(p_value);

    Func f0("f0");
    f0(x, y, c) = select(c == 0, 4.0f,             // Constant term
                      c == 1, p * 10.0f,        // Linear expression not in terms of a loop parameter
                      cast<float>(x) * 100.0f); // Linear expression in terms of x

    Image<float> out0(8, 8, 3);
    f0.bound(c, 0, 3);
    f0.glsl(x, y, c);

    // Run the test
    varyings.clear();
    f0.add_custom_lowering_pass(new CountVarying);
    f0.realize(out0);

    // Check for the correct number of varying attributes
    if (varyings.size() != 2) {
        fprintf(stderr,
                "Error: wrong number of varying attributes: %d should be %d\n",
                (int)varyings.size(), 2);
        return 1;
    }

    // Check for correct result values
    out0.copy_to_host();

    for (int c=0; c != out0.extent(2); ++c) {
        for (int y=0; y != out0.extent(1); ++y) {
            for (int x=0; x != out0.extent(0); ++x) {
                float expected;
                switch (c) {
                    case 0:
                        expected = 4.0f;
                        break;
                    case 1:
                        expected = p_value * 10.0f;
                        break;
                    default:
                        expected = static_cast<float>(x) * 100.0f;

                }
                float result = out0(x, y, c);
                if (result != expected) {
                    fprintf(stderr, "Incorrect value: %f != %f at %d,%d,%d.\n",
                            result, expected, x, y, c);
                    return 1;
                }
            }
        }
    }
    fprintf(stderr, "Passed!\n");

    // This is a more complicated test case where several expressions are linear
    // in all of the loop variables. This is the coordinate transformation case
    fprintf(stderr, "Test f1\n");

    float th = 3.141592f/8.0f;
    float s_th = sinf(th);
    float c_th = cosf(th);

    float m[] = {
        c_th, -s_th, 0.0f,
        s_th,  c_th, 0.0f
    };

    Param<float> m0("m0"), m1("m1"), m2("m2"),
                 m3("m3"), m4("m4"), m5("m5");

    m0.set(m[0]); m1.set(m[1]); m2.set(m[2]);
    m3.set(m[3]); m4.set(m[4]); m5.set(m[5]);

    Func f1("f1");
    f1(x, y, c) = select(c == 0, m0 * x + m1 * y + m2,
                       c == 1, m3 * x + m4 * y + m5,
                       1.0f);

    f1.bound(c, 0, 3);
    f1.glsl(x, y, c);

    Image<float> out1(8, 8, 3);

    // Run the test
    varyings.clear();
    f1.add_custom_lowering_pass(new CountVarying);
    f1.realize(out1);

    // Check for the correct number of varying attributes
    if (varyings.size() != 4) {
        fprintf(stderr,
                "Error: wrong number of varying attributes: %d should be %d\n",
                (int)varyings.size(), 4);
        return 1;
    }

    // Check for correct result values
    out1.copy_to_host();
    for (int c=0; c != out1.extent(2); ++c) {
        for (int y=0; y != out1.extent(1); ++y) {
            for (int x=0; x != out1.extent(0); ++x) {
                float expected;
                switch (c) {
                    case 0:
                        expected = m[0] * x + m[1] * y + m[2];
                        break;
                    case 1:
                        expected = m[3] * x + m[4] * y + m[5];
                        break;
                    default:
                        expected = 1.0f;

                }
                float result = out1(x, y, c);

                // There is no defined precision requirement in this case so an
                // arbitrary threshold is used to compare the result of the CPU
                // and GPU arithmetic
                if (fabs(result-expected) > 0.000001f) {
                    fprintf(stderr, "Incorrect value: %f != %f at %d,%d,%d.\n",
                            result, expected, x, y, c);
                    return 1;
                }
            }
        }
    }
    fprintf(stderr, "Passed!\n");

    // The feature is supposed to find linearly varying sub-expressions as well
    // so for example, if the above expressions are wrapped in a non-linear
    // function like sqrt, they should still be extracted.
    fprintf(stderr, "Test f2\n");
    Func f2("f2");
    f2(x, y, c) = select(c == 0, sqrt(m0 * x + m1 * y + m2),
                       c == 1, sqrt(m3 * x + m4 * y + m5),
                       1.0f);

    f2.bound(c, 0, 3);
    f2.glsl(x, y, c);

    Image<float> out2(8, 8, 3);

    // Run the test
    varyings.clear();
    f2.add_custom_lowering_pass(new CountVarying);
    f2.realize(out2);

    // Check for the correct number of varying attributes
    if (varyings.size() != 4) {
        fprintf(stderr,
                "Error: wrong number of varying attributes: %d should be %d\n",
                (int)varyings.size(), 4);
        return 1;
    }

    // Check for correct result values
    out2.copy_to_host();

    for (int c=0; c != out2.extent(2); ++c) {
        for (int y=0; y != out2.extent(1); ++y) {
            for (int x=0; x != out2.extent(0); ++x) {
                float expected;
                switch (c) {
                    case 0:
                        expected = sqrtf(m[0] * x + m[1] * y + m[2]);
                        break;
                    case 1:
                        expected = sqrtf(m[3] * x + m[4] * y + m[5]);
                        break;
                    default:
                        expected = 1.0f;

                }
                float result = out2(x, y, c);

                // There is no defined precision requirement in this case so an
                // arbitrary threshold is used to compare the result of the CPU
                // and GPU arithmetic
                if (fabs(result-expected) > 0.000001f) {
                    fprintf(stderr, "Incorrect value: %f != %f at %d,%d,%d.\n",
                            result, expected, x, y, c);
                    return 1;
                }
            }
        }
    }
    fprintf(stderr, "Passed!\n");

    // This case tests a large expression linearly varying in terms of a loop
    // variable
    fprintf(stderr, "Test f3\n");

    Expr foo = p;
    for (int i = 0; i < 10; i++) {
        foo = foo+foo+foo;
    }
    foo = x + foo;

    float foo_value = p_value;
    for (int i = 0; i < 10; i++) {
        foo_value = foo_value+foo_value+foo_value;
    }

    Func f3("f3");
    f3(x, y, c) = select(c == 0, foo,
                       c == 1, 1.0f,
                       2.0f);

    f3.bound(c, 0, 3);
    f3.glsl(x, y, c);

    Image<float> out3(8, 8, 3);

    // Run the test
    varyings.clear();
    f3.add_custom_lowering_pass(new CountVarying);
    f3.realize(out3);

    // Check for the correct number of varying attributes
    if (varyings.size() != 2) {
        fprintf(stderr,
                "Error: wrong number of varying attributes: %d should be %d\n",
                (int)varyings.size(), 2);
        return 1;
    }

    // Check for correct result values
    out3.copy_to_host();

    for (int c=0; c != out3.extent(2); ++c) {
        for (int y=0; y != out3.extent(1); ++y) {
            for (int x=0; x != out3.extent(0); ++x) {
                float expected;
                switch (c) {
                    case 0:
                        expected = (float)x + foo_value;
                        break;
                    case 1:
                        expected = 1.0f;
                        break;
                    default:
                        expected = 2.0f;

                }
                float result = out3(x, y, c);

                // There is no defined precision requirement in this case so an
                // arbitrary threshold is used to compare the result of the CPU
                // and GPU arithmetic
                if (fabs(result-expected) > 0.000001f) {
                    fprintf(stderr, "Incorrect value: %f != %f at %d,%d,%d.\n",
                            result, expected, x, y, c);
                    return 1;
                }
            }
        }
    }
    fprintf(stderr, "Passed!\n");

    // The test will return early on error.
    fprintf(stderr, "Success!\n");

    // This test may abort with the message "Failed to free device buffer" due
    // to https://github.com/halide/Halide/issues/559
    return 0;
}
