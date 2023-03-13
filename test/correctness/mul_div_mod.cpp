#include "Halide.h"
#include "test_sharding.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>

#ifdef _MSC_VER
// Silence a warning that is obscure, harmless, and painful to work around
#pragma warning(disable : 4146)  // unary minus operator applied to unsigned type, result still unsigned
#endif

using namespace Halide;
using Halide::Internal::Call;

// Test program to check basic arithmetic.
// Pseudo-random numbers are generated and arithmetic operations performed on them.
// To ensure that the extremes of the data values are included in testing, the upper
// left corner of each matrix contains the extremes.

// The code uses 64 bit arithmetic to ensure that results are correct in 32 bits and fewer,
// even if overflow occurs.

// Dimensions of the test data, and rate of salting with extreme values (1 in SALTRATE)
#define WIDTH 1024
#define HEIGHT 1024
#define SALTRATE 50
// Portion of the test data to use for testing the simplifier
#define SWIDTH 32
#define SHEIGHT HEIGHT

// Generate poor quality pseudo random numbers.
// For reproducibility, the array indices are used as the seed for each
// number generated.  The algorithm simply multiplies the seeds by large
// primes and combines them together, then multiplies by additional large primes.
// We don't want to use primes that are close to powers of 2 because they dont
// randomise the bits.
//
// unique: Use different values to get unique data in each array.
// i, j: Coordinates for which the value is being generated.
uint64_t ubits(int unique, int i, int j) {
    uint64_t bits, mi, mj, mk, ml, mu;
    mi = 982451653;  // 50 M'th prime
    mj = 776531491;  // 40 M'th prime
    mk = 573259391;  // 30 M'th prime
    ml = 373587883;  // 20 M'th prime
    mu = 275604541;  // 15 M'th prime
    // Each of the above primes is at least 10^8 i.e. at least 24 bits
    // so we are assured that the initial value computed below occupies 64 bits
    // and then the subsequent operations help ensure that every bit is affected by
    // all three inputs.

    bits = ((unique * mu + i) * mi + j) * mj;  // All multipliers are prime
    bits = (bits ^ (bits >> 32)) * mk;
    bits = (bits ^ (bits >> 32)) * ml;
    bits = (bits ^ (bits >> 32)) * mi;
    bits = (bits ^ (bits >> 32)) * mu;
    return bits;
}

// Template to avoid autological comparison errors when comparing unsigned values for < 0
template<typename T>
bool less_than_zero(T val) {
    return (val < 0);
}

template<>
bool less_than_zero<unsigned long long>(unsigned long long val) {
    return false;
}

template<>
bool less_than_zero<unsigned long>(unsigned long val) {
    return false;
}

template<>
bool less_than_zero<unsigned int>(unsigned int val) {
    return false;
}

template<>
bool less_than_zero<unsigned short>(unsigned short val) {
    return false;
}

template<>
bool less_than_zero<unsigned char>(unsigned char val) {
    return false;
}

template<typename T>
bool is_negative_one(T val) {
    return (val == -1);
}

template<>
bool is_negative_one(unsigned long long val) {
    return false;
}

template<>
bool is_negative_one(unsigned long val) {
    return false;
}

template<>
bool is_negative_one(unsigned int val) {
    return false;
}

template<>
bool is_negative_one(unsigned short val) {
    return false;
}

template<>
bool is_negative_one(unsigned char val) {
    return false;
}

template<typename T, typename BIG>
BIG maximum() {
    Type t = type_of<T>();

    if (t.is_float()) {
        return (BIG)1.0;
    }
    if (t.is_uint()) {
        uint64_t max;
        max = 0;
        max = ~max;
        if (t.bits() < 64)
            max = (((uint64_t)1) << t.bits()) - 1;
        return (BIG)max;
    }
    if (t.is_int()) {
        uint64_t umax;
        umax = (((uint64_t)1) << (t.bits() - 1)) - 1;
        return (BIG)umax;
    }
    assert(0);
    return (BIG)1;
}

template<typename T, typename BIG>
BIG minimum() {
    Type t = type_of<T>();

    if (t.is_float()) {
        return (BIG)0.0;
    }
    if (t.is_uint()) {
        return (BIG)0;
    }
    if (t.is_int()) {
        uint64_t umax;
        BIG min;
        umax = (((uint64_t)1) << (t.bits() - 1)) - 1;
        min = umax;
        min = -min - 1;
        return min;
    }
    assert(0);
    return (BIG)0;
}

// Construct an image for testing.
// Contents are poor quality pseudo-random numbers in the natural range for the specified type.
// The top left corner contains one of two patterns.  (Remember that first coordinate is column in Halide)
//  min  max      OR      min  max
//  min  max              max  min
// The left pattern occurs when unique is odd; the right pattern when unique is even.

template<typename T, typename BIG>
Buffer<T> init(Type t, int unique, int width, int height) {
    if (width < 2) width = 2;
    if (height < 2) height = 2;

    Buffer<T> result(width, height);

    if (t.is_int()) {
        // Signed integer type with specified number of bits.
        int64_t max, min, neg, v, vsalt;
        max = maximum<T, int64_t>();
        min = minimum<T, int64_t>();
        neg = (~((int64_t)0)) ^ max;  // The bits that should all be 1 for negative numbers.
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                v = (int64_t)(ubits(unique, i, j));
                if (v < 0)
                    v |= neg;  // Make all the high bits one
                else
                    v &= max;
                // Salting with extreme values
                vsalt = (int64_t)(ubits(unique | 0x100, i, j));
                if (vsalt % SALTRATE == 0) {
                    if (vsalt & 0x1000000) {
                        v = max;
                    } else {
                        v = min;
                    }
                }
                result(i, j) = (T)v;
            }
        }
        result(0, 0) = (T)min;
        result(1, 0) = (T)max;
        result(0, 1) = (T)((unique & 1) ? min : max);
        result(1, 1) = (T)((unique & 1) ? max : min);
    } else if (t.is_uint()) {
        uint64_t max, v, vsalt;
        max = maximum<T, BIG>();
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                v = ubits(unique, i, j) & max;
                // Salting with extreme values
                vsalt = (int64_t)(ubits(unique | 0x100, i, j));
                if (vsalt % SALTRATE == 0) {
                    if (vsalt & 0x1000000) {
                        v = max;
                    } else {
                        v = 0;
                    }
                }
                result(i, j) = (T)v;
            }
        }
        result(0, 0) = (T)0;
        result(1, 0) = (T)max;
        result(0, 1) = (T)((unique & 1) ? 0 : max);
        result(1, 1) = (T)((unique & 1) ? max : 0);
    } else if (t.is_float()) {
        uint64_t uv, vsalt;
        uint64_t max = (uint64_t)(-1);
        double v;
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                uv = ubits(unique, i, j);
                v = (((double)uv) / ((double)(max))) * 2.0 - 1.0;
                // Salting with extreme values
                vsalt = (int64_t)(ubits(unique | 0x100, i, j));
                if (vsalt % SALTRATE == 0) {
                    if (vsalt & 0x1000000) {
                        v = 1.0;
                    } else {
                        v = 0.0;
                    }
                }
                result(i, j) = (T)v;
            }
        }
        result(0, 0) = (T)(0.0);
        result(1, 0) = (T)(1.0);
        result(0, 1) = (T)((unique & 1) ? 0.0 : 1.0);
        result(1, 1) = (T)((unique & 1) ? 1.0 : 0.0);
    } else {
        printf("Unknown data type in init.\n");
    }

    return result;
}

enum ScheduleVariant {
    CPU,
    TiledGPU,
    Hexagon
};

// Test multiplication of T1 x T2 -> RT
template<typename T1, typename T2, typename RT, typename BIG>
bool mul(int vector_width, ScheduleVariant scheduling, const Target &target) {
    // std::cout << "Test multiplication of "
    //           << type_of<T1>() << "x" << vector_width << "*"
    //           << type_of<T2>() << "x" << vector_width << "->"
    //           << type_of<RT>() << "x" << vector_width << "\n";

    int i, j;
    Type t1 = type_of<T1>();
    Type t2 = type_of<T2>();
    Type rt = type_of<RT>();
    bool success = true;

    // The parameter bits can be used to control the maximum data value.
    Buffer<T1> a = init<T1, BIG>(t1, 1, WIDTH, HEIGHT);
    Buffer<T2> b = init<T2, BIG>(t2, 2, WIDTH, HEIGHT);

    // Compute the multiplication, check that the results match.
    Func f;
    Var x, y, xi, yi;
    f(x, y) = cast(rt, a(x, y)) * cast(rt, b(x, y));
    if (vector_width > 1) {
        f.vectorize(x, vector_width);
    }
    switch (scheduling) {
    case CPU:
        break;
    case TiledGPU:
        f.compute_root().gpu_tile(x, y, xi, yi, 16, 16);
        break;
    case Hexagon:
        f.compute_root().hexagon();
        break;
    };

    Buffer<RT> r = f.realize({WIDTH, HEIGHT}, target);

    int ecount = 0;
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            T1 ai = a(i, j);
            T2 bi = b(i, j);
            RT ri = r(i, j);
            RT correct = BIG(ai) * BIG(bi);
            if (correct != ri && (ecount++) < 10) {
                std::cerr << (int64_t)ai << "*" << (int64_t)bi << " -> " << (int64_t)ri << " != " << (int64_t)correct << "\n";
                success = false;
            }

            if (i < SWIDTH && j < SHEIGHT) {
                Expr ae = cast<RT>(Expr(ai));
                Expr be = cast<RT>(Expr(bi));
                Expr re = simplify(ae * be);

                if (Call::as_intrinsic(re, {Call::signed_integer_overflow})) {
                    // Don't check correctness of signed integer overflow.
                } else {
                    if (!Internal::equal(re, Expr(ri)) && (ecount++) < 10) {
                        std::cerr << "Compiled a*b != simplified a*b: " << (int64_t)ai
                                  << "*" << (int64_t)bi
                                  << " = " << (int64_t)ri
                                  << " != " << re << "\n";
                        success = false;
                    }
                }
            }
        }
    }

    return success;
}

// division tests division and mod operations.
// BIG should be uint64_t, int64_t or double as appropriate.
// T should be a type known to Halide.
template<typename T, typename BIG>
bool div_mod(int vector_width, ScheduleVariant scheduling, const Target &target) {
    // std::cout << "Test division of " << type_of<T>() << "x" << vector_width << "\n";

    int i, j;
    Type t = type_of<T>();
    BIG minval = minimum<T, BIG>();
    bool success = true;

    // The parameter bits can be used to control the maximum data value.
    Buffer<T> a = init<T, BIG>(t, 1, WIDTH, HEIGHT);
    Buffer<T> b = init<T, BIG>(t, 2, WIDTH, HEIGHT);

    // Filter the input values for the operation to be tested.
    // Cannot divide by zero, so remove zeroes from b.
    // Also, cannot divide the most negative number by -1.
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            if (b(i, j) == 0) {
                b(i, j) = 1;  // Replace zero with one
            }
            if (a(i, j) == minval && less_than_zero(minval) && is_negative_one(b(i, j))) {
                a(i, j) = a(i, j) + 1;  // Fix it into range.
            }
        }
    }

    // Compute division and mod, and check they satisfy the requirements of Euclidean division.
    Func f;
    Var x, y, xi, yi;
    f(x, y) = Tuple(a(x, y) / b(x, y), a(x, y) % b(x, y));  // Using Halide division operation.
    if (vector_width > 1) {
        f.vectorize(x, vector_width);
    }
    switch (scheduling) {
    case CPU:
        break;
    case TiledGPU:
        f.compute_root().gpu_tile(x, y, xi, yi, 16, 16);
        break;
    case Hexagon:
        f.compute_root().hexagon();
        break;
    };

    Realization R = f.realize({WIDTH, HEIGHT}, target);
    Buffer<T> q(R[0]);
    Buffer<T> r(R[1]);

    int ecount = 0;
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            T ai = a(i, j);
            T bi = b(i, j);
            T qi = q(i, j);
            T ri = r(i, j);

            if (BIG(qi) * BIG(bi) + ri != ai && (ecount++) < 10) {
                std::cerr << "\ndiv_mod failure for t=" << target << " w=" << vector_width << " scheduling=" << (int)scheduling << ":\n";
                std::cerr << "(a/b)*b + a%b != a; a, b = " << (int64_t)ai
                          << ", " << (int64_t)bi
                          << "; q, r = " << (int64_t)qi
                          << ", " << (int64_t)ri << "\n";
                success = false;
            } else if (!(0 <= ri &&
                         (t.is_min((int64_t)bi) || ri < (T)std::abs((int64_t)bi))) &&
                       (ecount++) < 10) {
                std::cerr << "\ndiv_mod failure for t=" << target << " w=" << vector_width << " scheduling=" << (int)scheduling << ":\n";
                std::cerr << "ri is not in the range [0, |b|); a, b = " << (int64_t)ai
                          << ", " << (int64_t)bi
                          << "; q, r = " << (int64_t)qi
                          << ", " << (int64_t)ri << "\n";
                success = false;
            }

            if (i < SWIDTH && j < SHEIGHT) {
                Expr ae = Expr(ai);
                Expr be = Expr(bi);
                Expr qe = simplify(ae / be);
                Expr re = simplify(ae % be);

                if (!Internal::equal(qe, Expr(qi)) && (ecount++) < 10) {
                    std::cerr << "\ndiv_mod failure for t=" << target << " w=" << vector_width << " scheduling=" << (int)scheduling << ":\n";
                    std::cerr << "Compiled a/b != simplified a/b: " << (int64_t)ai
                              << "/" << (int64_t)bi
                              << " = " << (int64_t)qi
                              << " != " << qe << "\n";
                    success = false;
                } else if (!Internal::equal(re, Expr(ri)) && (ecount++) < 10) {
                    std::cerr << "\ndiv_mod failure for t=" << target << " w=" << vector_width << " scheduling=" << (int)scheduling << ":\n";
                    std::cerr << "Compiled a%b != simplified a%b: " << (int64_t)ai
                              << "%" << (int64_t)bi
                              << " = " << (int64_t)ri
                              << " != " << re << "\n";
                    success = false;
                }
            }
        }
    }

    return success;
}

// f_mod tests floating mod operations.
// BIG should be double.
// T should be a type known to Halide.
template<typename T, typename BIG>
bool f_mod() {
    // std::cout << "Test mod of " << type_of<T>() << "\n";

    int i, j;
    Type t = type_of<T>();
    bool success = true;

    Buffer<T> a = init<T, BIG>(t, 1, WIDTH, HEIGHT);
    Buffer<T> b = init<T, BIG>(t, 2, WIDTH, HEIGHT);
    Buffer<T> out(WIDTH, HEIGHT);

    // Filter the input values for the operation to be tested.
    // Cannot divide by zero, so remove zeroes from b.
    for (i = 0; i < WIDTH; i++) {
        for (j = 0; j < HEIGHT; j++) {
            if (b(i, j) == 0.0) {
                b(i, j) = 1.0;  // Replace zero with one.
            }
        }
    }

    // Compute modulus result and check it.
    Func f;
    f(_) = a(_) % b(_);  // Using Halide mod operation.
    f.realize(out);

    // Explicit checks of the simplifier for consistency with runtime computation
    int ecount = 0;
    for (i = 0; i < std::min(SWIDTH, WIDTH); i++) {
        for (j = 0; j < std::min(SHEIGHT, HEIGHT); j++) {
            T arg_a = a(i, j);
            T arg_b = b(i, j);
            T v = out(i, j);
            Expr in_e = cast<T>((float)arg_a) % cast<T>((float)arg_b);
            Expr e = simplify(in_e);
            Expr eout = cast<T>((float)v);
            if (!Internal::equal(e, eout) && (ecount++) < 10) {
                Expr diff = simplify(e - eout);
                Expr smalldiff = simplify(diff < (float)(0.000001) && diff > (float)(-0.000001));
                if (!Internal::is_const_one(smalldiff)) {
                    std::cerr << "simplify(" << in_e << ") yielded " << e << "; expected " << eout << "\n";
                    std::cerr << "          difference=" << diff << "\n";
                    success = false;
                }
            }
        }
    }

    return success;
}

struct Task {
    std::function<bool()> fn;
};

void add_test_mul(int vector_width, ScheduleVariant scheduling, Target target, std::vector<Task> &tasks) {
    // Non-widening multiplication.
    tasks.push_back({[=]() { return mul<uint8_t, uint8_t, uint8_t, uint64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return mul<uint16_t, uint16_t, uint16_t, uint64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return mul<uint32_t, uint32_t, uint32_t, uint64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return mul<int8_t, int8_t, int8_t, int64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return mul<int16_t, int16_t, int16_t, int64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return mul<int32_t, int32_t, int32_t, int64_t>(vector_width, scheduling, target); }});

    // Widening multiplication.
    tasks.push_back({[=]() { return mul<uint8_t, uint8_t, uint16_t, uint64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return mul<uint16_t, uint16_t, uint32_t, uint64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return mul<int8_t, int8_t, int16_t, int64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return mul<int16_t, int16_t, int32_t, int64_t>(vector_width, scheduling, target); }});

    // Mixed multiplication. This isn't all of the possible mixed
    // multiplications, but it covers all of the special cases we
    // have in Halide.
    tasks.push_back({[=]() { return mul<uint16_t, uint32_t, uint32_t, uint64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return mul<int16_t, int32_t, int32_t, int64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return mul<uint16_t, int32_t, int32_t, uint64_t>(vector_width, scheduling, target); }});
}

void add_test_div_mod(int vector_width, ScheduleVariant scheduling, Target target, std::vector<Task> &tasks) {
    tasks.push_back({[=]() { return div_mod<uint8_t, uint64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return div_mod<uint16_t, uint64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return div_mod<uint32_t, uint64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return div_mod<int8_t, int64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return div_mod<int16_t, int64_t>(vector_width, scheduling, target); }});
    tasks.push_back({[=]() { return div_mod<int32_t, int64_t>(vector_width, scheduling, target); }});
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    ScheduleVariant scheduling = CPU;
    if (target.has_gpu_feature()) {
        scheduling = TiledGPU;
    } else if (target.has_feature(Target::HVX)) {
        scheduling = Hexagon;
    }

    // Test multiplication and division
    std::vector<int> vector_widths = {1};
    if (target.has_feature(Target::Metal) ||
        target.has_feature(Target::D3D12Compute) ||
        target.has_feature(Target::WebGPU)) {
        for (int i = 2; i <= 4; i *= 2) {
            vector_widths.push_back(i);
        }
    } else if (target.has_feature(Target::OpenGLCompute)) {
        // Vector load/store unimplemented
    } else if (target.has_feature(Target::HVX)) {
        vector_widths.push_back(128);
    } else {
        for (int i = 2; i <= 16; i *= 2) {
            vector_widths.push_back(i);
        }
    }

    std::vector<Task> tasks;
    for (int vector_width : vector_widths) {
        add_test_mul(vector_width, scheduling, target, tasks);
    }
    for (int vector_width : vector_widths) {
        add_test_div_mod(vector_width, scheduling, target, tasks);
    }

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        if (!task.fn()) {
            exit(1);
        }
    }

    printf("Success!\n");
    return 0;
}
