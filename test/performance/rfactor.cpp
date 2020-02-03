#include "Halide.h"
#include "halide_benchmark.h"
#include <memory>
#include <stdio.h>

using namespace Halide;
using namespace Halide::Tools;

// Controls the size of the input data
#define N1 4
#define N2 4

int one_d_max() {
    const int size = 1024 * 1024 * N1 * N2;

    ImageParam A(Float(32), 1);

    RDom r(0, size);

    Func max_ref("max_ref");
    max_ref() = 0.0f;
    max_ref() = max(max_ref(), abs(A(r)));

    Func maxf("maxf");
    maxf() = 0.0f;
    RVar rxo, rxi, rxio, rxii;
    maxf() = max(maxf(), abs(A(r)));
    maxf.update().split(r.x, rxo, rxi, 4 * 8192);

    Var u, v;
    Func intm = maxf.update().rfactor(rxo, u);
    intm.compute_root()
        .update()
        .parallel(u)
        .split(rxi, rxio, rxii, 8)
        .rfactor(rxii, v)
        .compute_at(intm, u)
        .vectorize(v)
        .update()
        .vectorize(v);

    Buffer<float> vec_A(size);
    Buffer<float> ref_output = Buffer<float>::make_scalar();
    Buffer<float> output = Buffer<float>::make_scalar();

    // init randomly
    for (int ix = 0; ix < size; ix++) {
        vec_A(ix) = rand();
    }

    A.set(vec_A);

    double t_ref = benchmark([&]() {
        max_ref.realize(ref_output);
    });
    double t = benchmark([&]() {
        maxf.realize(output);
    });

    float gbits = 32.0f * size / 1e9f;  // bits per seconds

    printf("Max ref: %fms, %f Gbps\n", t_ref * 1e3, (gbits / t_ref));
    printf("Max with rfactor: %fms, %f Gbps\n", t * 1e3, (gbits / t));
    double improve = t_ref / t;
    printf("Improvement: %f\n\n", improve);

    return (improve > 0.9) ? 0 : -1;
}

int two_d_histogram() {
    int W = 1024 * N1, H = 1024 * N2;

    Buffer<uint8_t> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand();
        }
    }

    Func hist("hist"), ref("ref");
    Var x, y;

    RDom r(0, W, 0, H);

    ref(x) = 0;
    ref(in(r.x, r.y)) += 1;

    hist(x) = 0;
    hist(in(r.x, r.y)) += 1;

    Var u;
    RVar ryo, ryi;
    hist
        .update()
        .split(r.y, ryo, ryi, 16)
        .rfactor(ryo, u)
        .compute_root()
        .vectorize(x, 8)
        .update()
        .parallel(u);
    hist.update().vectorize(x, 8);

    ref.realize(256);
    hist.realize(256);

    Buffer<int> result(256);
    double t_ref = benchmark([&]() {
        ref.realize(result);
    });
    double t = benchmark([&]() {
        hist.realize(result);
    });

    double gbits = in.type().bits() * W * H / 1e9;  // bits per seconds

    printf("Histogram ref: %fms, %f Gbps\n", t_ref * 1e3, (gbits / t_ref));
    printf("Histogram with rfactor: %fms, %f Gbps\n", t * 1e3, (gbits / t));
    double improve = t_ref / t;
    printf("Improvement: %f\n\n", improve);

    return (improve > 0.9) ? 0 : -1;
}

int four_d_argmin() {
    const int size = 64;

    Func amin("amin"), ref("ref");

    ImageParam input(UInt(8), 4);

    RDom r(0, size, 0, size, 0, size, 0, size);

    ref() = Tuple(255, 0, 0, 0, 0);
    ref() = Tuple(min(ref()[0], input(r.x, r.y, r.y, r.z)),
                  select(ref()[0] < input(r.x, r.y, r.y, r.z), ref()[1], r.x),
                  select(ref()[0] < input(r.x, r.y, r.y, r.z), ref()[2], r.y),
                  select(ref()[0] < input(r.x, r.y, r.z, r.w), ref()[3], r.z),
                  select(ref()[0] < input(r.x, r.y, r.z, r.w), ref()[4], r.w));

    amin() = Tuple(255, 0, 0, 0, 0);
    amin() = Tuple(min(amin()[0], input(r.x, r.y, r.z, r.w)),
                   select(amin()[0] < input(r.x, r.y, r.z, r.w), amin()[1], r.x),
                   select(amin()[0] < input(r.x, r.y, r.z, r.w), amin()[2], r.y),
                   select(amin()[0] < input(r.x, r.y, r.z, r.w), amin()[3], r.z),
                   select(amin()[0] < input(r.x, r.y, r.z, r.w), amin()[4], r.w));

    Var u;
    Func intm1 = amin.update(0).rfactor(r.w, u);
    intm1.compute_root();
    intm1.update(0).parallel(u);

    Var v;
    RVar rxo, rxi;
    Func intm2 = intm1.update(0).split(r.x, rxo, rxi, 16).rfactor(rxi, v);
    intm2.compute_at(intm1, u);
    intm2.update(0).vectorize(v);

    Buffer<uint8_t> vec(size, size, size, size);

    // init randomly
    for (int iw = 0; iw < size; iw++) {
        for (int iz = 0; iz < size; iz++) {
            for (int iy = 0; iy < size; iy++) {
                for (int ix = 0; ix < size; ix++) {
                    vec(ix, iy, iz, iw) = (rand() % size);
                }
            }
        }
    }

    input.set(vec);

    ref.realize();
    amin.realize();

    double t_ref = benchmark([&]() {
        ref.realize();
    });
    double t = benchmark([&]() {
        amin.realize();
    });

    float gbits = input.type().bits() * vec.number_of_elements() / 1e9;  // bits per seconds

    printf("Argmin ref: %fms, %f Gbps\n", t_ref * 1e3, (gbits / t_ref));
    printf("Argmin with rfactor: %fms, %f Gbps\n", t * 1e3, (gbits / t));
    double improve = t_ref / t;
    printf("Improvement: %f\n\n", improve);

    return (improve > 0.9) ? 0 : -1;
}

int complex_multiply() {
    const int size = 1024 * 1024 * N1 * N2;

    Func mult("mult"), ref("ref");

    // TODO: change to float
    ImageParam input0(Int(32), 1);
    ImageParam input1(Int(32), 1);

    RDom r(0, size);

    ref() = Tuple(1, 0);
    ref() = Tuple(ref()[0] * input0(r.x) - ref()[1] * input1(r.x),
                  ref()[0] * input1(r.x) + ref()[1] * input0(r.x));

    mult() = Tuple(1, 0);
    mult() = Tuple(mult()[0] * input0(r.x) - mult()[1] * input1(r.x),
                   mult()[0] * input1(r.x) + mult()[1] * input0(r.x));

    RVar rxi, rxo, rxii, rxio;
    mult.update(0).split(r.x, rxo, rxi, 2 * 8192);

    Var u, v;
    Func intm = mult.update().rfactor(rxo, u);
    intm.compute_root()
        .vectorize(u, 8)
        .update()
        .parallel(u)
        .split(rxi, rxio, rxii, 8)
        .rfactor(rxii, v)
        .compute_at(intm, u)
        .vectorize(v)
        .update()
        .vectorize(v);

    Buffer<int32_t> vec0(size), vec1(size);

    // init randomly
    for (int ix = 0; ix < size; ix++) {
        vec0(ix) = (rand() % size);
        vec1(ix) = (rand() % size);
    }

    input0.set(vec0);
    input1.set(vec1);

    ref.realize();
    mult.realize();

    double t_ref = benchmark([&]() {
        ref.realize();
    });
    double t = benchmark([&]() {
        mult.realize();
    });

    float gbits = input0.type().bits() * size * 2 / 1e9;  // bits per seconds

    printf("Complex-multiply ref: %fms, %f Gbps\n", t_ref * 1e3, (gbits / t_ref));
    printf("Complex-multiply with rfactor: %fms, %f Gbps\n", t * 1e3, (gbits / t));
    double improve = t_ref / t;
    printf("Improvement: %f\n\n", improve);

    return (improve > 0.9) ? 0 : -1;
}

int dot_product() {
    const int size = 1024 * 1024 * N1 * N2;

    ImageParam A(Float(32), 1);
    ImageParam B(Float(32), 1);

    Param<int> p;

    RDom r(0, size);

    // Reference implementation
    Func dot_ref("dot_ref");
    dot_ref() = 0.0f;
    dot_ref() += (A(r.x)) * B(r.x);

    Func dot("dot");
    dot() = 0.0f;
    dot() += (A(r.x)) * B(r.x);
    RVar rxo, rxi, rxio, rxii;
    dot.update().split(r.x, rxo, rxi, 4 * 8192);

    Var u, v;
    Func intm = dot.update().rfactor(rxo, u);
    intm.compute_root()
        .update()
        .parallel(u)
        .split(rxi, rxio, rxii, 8)
        .rfactor(rxii, v)
        .compute_at(intm, u)
        .vectorize(v)
        .update()
        .vectorize(v);

    Buffer<float> vec_A(size), vec_B(size);
    Buffer<float> ref_output = Buffer<float>::make_scalar();
    Buffer<float> output = Buffer<float>::make_scalar();

    // init randomly
    for (int ix = 0; ix < size; ix++) {
        vec_A(ix) = rand();
        vec_B(ix) = rand();
    }

    A.set(vec_A);
    B.set(vec_B);

    double t_ref = benchmark([&]() {
        dot_ref.realize(ref_output);
    });
    double t = benchmark([&]() {
        dot.realize(output);
    });

    // Note that LLVM autovectorizes the reference!

    float gbits = 32 * size * (2 / 1e9f);  // bits per seconds

    printf("Dot-product ref: %fms, %f Gbps\n", t_ref * 1e3, (gbits / t_ref));
    printf("Dot-product with rfactor: %fms, %f Gbps\n", t * 1e3, (gbits / t));
    double improve = t_ref / t;
    printf("Improvement: %f\n\n", improve);

    return (improve > 0.9) ? 0 : -1;
}

int kitchen_sink() {
    const int size = 1024 * 1024 * N1 * N2;

    ImageParam A(Int(32), 1);

    RDom r(0, size);

    Func sink_ref("sink_ref");
    sink_ref() = {0, 0, int(0x80000000), 0, int(0x7fffffff), 0, 0, 0};
    sink_ref() = {
        sink_ref()[0] * A(r),                            // Product
        sink_ref()[1] + A(r),                            // Sum
        max(sink_ref()[2], A(r)),                        // Max
        select(sink_ref()[2] > A(r), sink_ref()[3], r),  // Argmax
        min(sink_ref()[4], A(r)),                        // Min
        select(sink_ref()[4] < A(r), sink_ref()[5], r),  // Argmin
        sink_ref()[6] + A(r) * A(r),                     // Sum of squares
        sink_ref()[7] + select(A(r) % 2 == 0, 1, 0)      // Number of even items
    };

    Func sink("sink");
    sink() = {0, 0, int(0x80000000), 0, int(0x7fffffff), 0, 0, 0};
    sink() = {
        sink()[0] * A(r),                        // Product
        sink()[1] + A(r),                        // Sum
        max(sink()[2], A(r)),                    // Max
        select(sink()[2] > A(r), sink()[3], r),  // Argmax
        min(sink()[4], A(r)),                    // Min
        select(sink()[4] < A(r), sink()[5], r),  // Argmin
        sink()[6] + A(r) * A(r),                 // Sum of squares
        sink()[7] + select(A(r) % 2 == 0, 1, 0)  // Number of even items
    };

    RVar rxo, rxi, rxio, rxii;
    sink.update().split(r.x, rxo, rxi, 8192);

    Var u, v;
    Func intm = sink.update().rfactor(rxo, u);
    intm.compute_root()
        .update()
        .parallel(u)
        .split(rxi, rxio, rxii, 8)
        .rfactor(rxii, v)
        .compute_at(intm, u)
        .vectorize(v)
        .update()
        .vectorize(v);

    Buffer<int32_t> vec_A(size);

    // init randomly
    for (int ix = 0; ix < size; ix++) {
        vec_A(ix) = rand();
    }

    A.set(vec_A);

    double t_ref = benchmark([&]() {
        sink_ref.realize();
    });
    double t = benchmark([&]() {
        sink.realize();
    });

    float gbits = 8 * size * (2 / 1e9f);  // bits per seconds

    printf("Kitchen sink ref: %fms, %f Gbps\n", t_ref * 1e3, (gbits / t_ref));
    printf("Kitchen sink with rfactor: %fms, %f Gbps\n", t * 1e3, (gbits / t));
    double improve = t_ref / t;
    printf("Improvement: %f\n\n", improve);

    return (improve > 0.9) ? 0 : -1;
}

int main(int argc, char **argv) {
    one_d_max();
    two_d_histogram();
    four_d_argmin();
    complex_multiply();
    dot_product();
    kitchen_sink();

    printf("Success!\n");
    return 0;
}
