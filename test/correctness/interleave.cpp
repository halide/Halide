#include "Halide.h"
#include <math.h>
#include <stdio.h>

using std::vector;
using namespace Halide;
using namespace Halide::Internal;

class CountInterleaves : public IRVisitor {
public:
    int result;
    CountInterleaves()
        : result(0) {
    }

    using IRVisitor::visit;

    void visit(const Shuffle *op) override {
        if (op->is_interleave()) {
            result++;
        }
        IRVisitor::visit(op);
    }
};

int count_interleaves(Func f) {
    Target t = get_jit_target_from_environment();
    t.set_feature(Target::NoBoundsQuery);
    t.set_feature(Target::NoAsserts);
    f.compute_root();
    std::vector<Module> submodules;
    Stmt s = Internal::lower_main_stmt({f.function()}, f.name(), t);
    CountInterleaves i;
    s.accept(&i);
    return i.result;
}

void check_interleave_count(Func f, int correct) {
    int c = count_interleaves(f);
    if (c < correct) {
        printf("Func %s should have interleaved >= %d times but interleaved %d times instead.\n",
               f.name().c_str(), correct, c);
        exit(1);
    }
}

void define(FuncRef f, std::vector<Expr> values) {
    if (values.size() == 1) {
        f = values[0];
    } else {
        f = Tuple(values);
    }
}

void define(FuncRef f, Expr value, int count) {
    std::vector<Expr> values;
    for (int i = 0; i < count; i++) {
        values.push_back(value);
    }
    define(f, values);
}

Expr element(FuncRef f, int i) {
    if (f.size() == 1) {
        assert(i == 0);
        return f;
    } else {
        return f[i];
    }
}

// Make sure the interleave pattern generates good vector code

int main(int argc, char **argv) {
    Var x, y, c;

    // TODO: Is this still true?
    // As of May 26 2016, this test causes a segfault due to
    // permissions failure on ARM-32 trying to execute a
    // non-executable page when jitting. Started happening between
    // llvm commits 270148 and 270159, but there's no obvious
    // culprit. Just disabling it for now.
    {
        Target t = get_host_target();
        if (t.arch == Target::ARM && t.bits == 32) {
            printf("[SKIP] Test is known to segfault on ARM-32 (see the source for more detail) .\n");
            return 0;
        }
    }

    for (int elements = 1; elements <= 5; elements++) {
        Func f, g, h;
        std::vector<Expr> f_def, g_def;
        for (int i = 0; i < elements; i++) {
            f_def.push_back(sin(x + i));
            g_def.push_back(cos(x + i));
        }
        define(f(x), f_def);
        define(g(x), g_def);
        std::vector<Expr> h_def;
        for (int i = 0; i < elements; i++) {
            h_def.push_back(select(x % 2 == 0, element(f(x / 2), i), element(g(x / 2), i) * 17.0f));
            g_def.push_back(cos(x + i));
        }
        define(h(x), h_def);

        f.compute_root();
        g.compute_root();
        h.vectorize(x, 8);

        check_interleave_count(h, 1);

        Realization results = h.realize({16});
        for (int i = 0; i < elements; i++) {
            Buffer<float> result = results[i];
            for (int x = 0; x < 16; x++) {
                float correct = ((x % 2) == 0) ? ((sinf(x / 2 + i))) : (cosf(x / 2 + i) * 17.0f);
                float delta = result(x) - correct;
                if (delta > 0.01 || delta < -0.01) {
                    printf("result(%d) = %f instead of %f\n", x, result(x), correct);
                    return 1;
                }
            }
        }
    }

    {
        // Test interleave 3 vectors:
        Func planar, interleaved;
        planar(x, y) = Halide::cast<float>(3 * x + y);
        interleaved(x, y) = planar(x, y);

        Var xy("xy");
        planar
            .compute_at(interleaved, xy)
            .vectorize(x, 4);

        interleaved
            .reorder(y, x)
            .bound(y, 0, 3)
            .bound(x, 0, 16)
            .fuse(y, x, xy)
            .vectorize(xy, 12);

        interleaved
            .output_buffer()
            .dim(0)
            .set_stride(3)
            .dim(1)
            .set_min(0)
            .set_stride(1)
            .set_extent(3);

        Buffer<float> buff3(3, 16);
        buff3.transpose(0, 1);

        interleaved.realize(buff3);

        check_interleave_count(interleaved, 1);

        for (int x = 0; x < 16; x++) {
            for (int y = 0; y < 3; y++) {
                float correct = 3 * x + y;
                float delta = buff3(x, y) - correct;
                if (delta > 0.01 || delta < -0.01) {
                    printf("result(%d) = %f instead of %f\n", x, buff3(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        // Test interleave 4 vectors:
        Func f1, f2, f3, f4, f5;
        f1(x) = sin(x);
        f2(x) = sin(2 * x);
        f3(x) = sin(3 * x);
        f4(x) = sin(4 * x);
        f5(x) = sin(5 * x);

        Func output4;
        output4(x, y) = select(y == 0, f1(x),
                               y == 1, f2(x),
                               y == 2, f3(x),
                               f4(x));

        output4
            .reorder(y, x)
            .bound(y, 0, 4)
            .unroll(y)
            .vectorize(x, 4);

        output4.output_buffer()
            .dim(0)
            .set_stride(4)
            .dim(1)
            .set_min(0)
            .set_stride(1)
            .set_extent(4);

        check_interleave_count(output4, 1);

        Buffer<float> buff4(4, 16);
        buff4.transpose(0, 1);

        output4.realize(buff4);

        for (int x = 0; x < 16; x++) {
            for (int y = 0; y < 4; y++) {
                float correct = sin((y + 1) * x);
                float delta = buff4(x, y) - correct;
                if (delta > 0.01 || delta < -0.01) {
                    printf("result(%d) = %f instead of %f\n", x, buff4(x, y), correct);
                    return 1;
                }
            }
        }

        // Test interleave 5 vectors:
        Func output5;
        output5(x, y) = select(y == 0, f1(x),
                               y == 1, f2(x),
                               y == 2, f3(x),
                               y == 3, f4(x),
                               f5(x));

        output5
            .reorder(y, x)
            .bound(y, 0, 5)
            .unroll(y)
            .vectorize(x, 4);

        output5.output_buffer()
            .dim(0)
            .set_stride(5)
            .dim(1)
            .set_min(0)
            .set_stride(1)
            .set_extent(5);

        check_interleave_count(output5, 1);

        Buffer<float> buff5(5, 16);
        buff5.transpose(0, 1);

        output5.realize(buff5);

        for (int x = 0; x < 16; x++) {
            for (int y = 0; y < 5; y++) {
                float correct = sin((y + 1) * x);
                float delta = buff5(x, y) - correct;
                if (delta > 0.01 || delta < -0.01) {
                    printf("result(%d) = %f instead of %f\n", x, buff5(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        // Test interleaving inside of nested blocks
        Func f1, f2, f3, f4, f5;
        f1(x) = sin(x);
        f1.compute_root();

        f2(x) = sin(2 * x);
        f2.compute_root();

        Func unrolled;
        unrolled(x, y) = select(x % 2 == 0, f1(x), f2(x)) + y;

        Var xi, yi;
        unrolled.tile(x, y, xi, yi, 16, 2).unroll(xi, 2).vectorize(xi, 4).unroll(xi).unroll(yi);

        check_interleave_count(unrolled, 4);
    }

    for (int elements = 1; elements <= 5; elements++) {
        const Target t = get_jit_target_from_environment();
        if (t.arch == Target::WebAssembly &&
            t.has_feature(Target::WasmSimd128) &&
            elements == 5) {
            // TODO: this bug is still active in v7.5; when it is fixed,
            // find a way to re-enable this test iff we are using the appropriate
            // version of v8.
            printf("Skipping part of correctness_interleave test for WebAssembly+WasmSimd128 due to https://bugs.chromium.org/p/v8/issues/detail?id=9083.\n");
            continue;
        }

        // Make sure we don't interleave when the reordering would change the meaning.
        Realization *refs = nullptr;
        for (int i = 0; i < 2; i++) {
            Func output6;
            define(output6(x, y), cast<uint8_t>(x), elements);
            RDom r(0, 16);

            // A not-safe-to-merge pair of updates
            define(output6(2 * r, 0), cast<uint8_t>(3), elements);
            define(output6(2 * r + 1, 0), cast<uint8_t>(4), elements);

            // A safe-to-merge pair of updates
            define(output6(2 * r, 1), cast<uint8_t>(3), elements);
            define(output6(2 * r + 1, 1), cast<uint8_t>(4), elements);

            // A safe-to-merge-but-not-complete triple of updates:
            define(output6(3 * r, 3), cast<uint8_t>(3), elements);
            define(output6(3 * r + 1, 3), cast<uint8_t>(4), elements);

            // A safe-to-merge-but-we-don't pair of updates, because they
            // load recursively, so we conservatively bail out.
            std::vector<Expr> rdef0, rdef1;
            for (int i = 0; i < elements; i++) {
                rdef0.push_back(element(output6(2 * r, 2), i) + 1);
                rdef1.push_back(element(output6(2 * r + 1, 2), i) + 1);
            }
            define(output6(2 * r, 2), rdef0);
            define(output6(2 * r + 1, 2), rdef1);

            // A safe-to-merge triple of updates:
            define(output6(3 * r, 3), cast<uint8_t>(7), elements);
            define(output6(3 * r + 2, 3), cast<uint8_t>(9), elements);
            define(output6(3 * r + 1, 3), cast<uint8_t>(8), elements);

            if (i == 0) {
                // Making the reference output.
                refs = new Realization(output6.realize({50, 4}));
            } else {
                // Vectorize and compare to the reference.
                for (int j = 0; j < 11; j++) {
                    output6.update(j).vectorize(r);
                }

                check_interleave_count(output6, 2 * elements);

                Realization outs = output6.realize({50, 4});
                for (int e = 0; e < elements; e++) {
                    Buffer<uint8_t> ref = (*refs)[e];
                    Buffer<uint8_t> out = outs[e];
                    for (int y = 0; y < ref.height(); y++) {
                        for (int x = 0; x < ref.width(); x++) {
                            if (out(x, y) != ref(x, y)) {
                                printf("result(%d, %d) = %d instead of %d\n",
                                       x, y, out(x, y), ref(x, y));
                                return 1;
                            }
                        }
                    }
                }
            }
        }
        delete refs;
    }

    for (int sz : {8, 27, 256}) {
        // Test transposition at a reasonable size (8), at a weird
        // size (27), and at a totally unreasonable size (256) to make sure
        // nothing crashes at least (256 x 256 would overflow the
        // number of vector lanes we can represent)
        Func square("square");
        square(x, y) = cast(UInt(16), 5 * x + y);

        Func trans("trans2");
        trans(x, y) = square(y, x);

        square.compute_root()
            .bound(x, 0, sz)
            .bound(y, 0, sz);

        trans.compute_root()
            .bound(x, 0, sz)
            .bound(y, 0, sz)
            .unroll(x)
            .vectorize(y);

        trans.output_buffer()
            .dim(0)
            .set_min(0)
            .set_stride(1)
            .set_extent(sz)
            .dim(1)
            .set_min(0)
            .set_stride(sz)
            .set_extent(sz);

        if (sz < 256) {
            // LLVM chokes on the 256x256 case
            Buffer<uint16_t> result7(sz, sz);
            trans.realize(result7);

            for (int x = 0; x < sz; x++) {
                for (int y = 0; y < sz; y++) {
                    int correct = 5 * y + x;
                    if (result7(x, y) != correct) {
                        printf("result(%d) = %d instead of %d\n", x, result7(x, y), correct);
                        return 1;
                    }
                }
            }
            check_interleave_count(trans, 1);
        } else {
            // We don't expect an interleave at 256 x 256
            check_interleave_count(trans, 0);
        }
    }

    printf("Success!\n");
    return 0;
}
