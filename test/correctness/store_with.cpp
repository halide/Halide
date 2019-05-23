#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {

    if (1) {
        // Pointwise parallel in-place
        Func f, g;
        Var x;
        f(x) = x;
        g(x) = f(x) + 3;
        f.compute_root().store_with(g);
        g.vectorize(x, 8, TailStrategy::RoundUp).parallel(x);
        f.vectorize(x, 4, TailStrategy::RoundUp).parallel(x);
        Buffer<int> buf = g.realize(128);

        for (int i = 0; i < 100; i++) {
            int correct = i + 3;
            if (buf(i) != correct) {
                printf("%d: buf(%d) = %d instead of %d\n", __LINE__, i, buf(i), correct);
                return -1;
            }
        }
    }

    if (1) {
        // A scan done directly within the output buffer to elide a copy.
        Func f, g;
        Var x, y;

        f(x, y) = x + y;
        RDom r(0, 99);
        f(r + 1, y) += f(r, y);
        f(98 - r, y) += f(99 - r, y);
        g(x, y) = f(x, y);

        g.unroll(y, 5, TailStrategy::RoundUp);

        f.compute_at(g, y).store_with(g);

        Buffer<int> buf = g.realize(100, 100);

        for (int y = 0; y < 1; y++) {
            int correct[100];
            for (int x = 0; x < 100; x++) {
                correct[x] = x + y;
            }
            for (int x = 0; x < 99; x++) {
                correct[x + 1] += correct[x];
            }
            for (int x = 0; x < 99; x++) {
                correct[98 - x] += correct[99 - x];
            }

            for (int x = 0; x < 100; x++) {
                if (buf(x, y) != correct[x]) {
                    printf("%d: buf(%d, %d) = %d instead of %d\n", __LINE__, x, y, buf(x, y), correct[x]);
                }
            }
        }
    }

    if (1) {
        // Move an array one vector to the left, in-place
        Func f, g, h;
        Var x;

        f(x) = x;
        g(x) = f(x+8);
        h(x) = g(x);

        f.compute_at(g, x).vectorize(x, 8, TailStrategy::GuardWithIf);

        f.store_with(g);
        g.compute_root();
        h.compute_root();

        Buffer<int> buf = h.realize(100);

        for (int i = 0; i < 100; i++) {
            int correct = i + 8;
            if (buf(i) != correct) {
                printf("%d: buf(%d) = %d instead of %d\n", __LINE__, i, buf(i), correct);
                return -1;
            }
        }
    }

    if (0) {
        // Set the odd entries to be some function of the even entries. This can be done in-place.
        Func f, g;
        Var x;

        f(x) = x;
        g(x) = select(x % 2 == 0, undef<int>(), f((x - 1) % 100) + f((x + 1) % 100));

        f.compute_root().store_with(g);

        Buffer<int> buf = g.realize(100);

        for (int i = 0; i < 100; i++) {
            int correct = (i % 2 == 1) ? ((i + 99) % 100 + (i + 1) % 100) : i;
            if (buf(i) != correct) {
                printf("%d: buf(%d) = %d instead of %d\n", __LINE__, i, buf(i), correct);
                return -1;
            }
        }
    }

    if (1) {
        // Zero-copy concat by having the two args write directly into
        // the destination buffer.
        Func f, g, h;
        Var x, y;

        f(x) = 18701;
        g(x) = 345;
        h(x) = select(x < 100, f(x), g(x - 100));

        f.compute_root().store_with(h);
        g.compute_root().store_with(h, {x + 100});
        h.bound(x, 0, 200);
        Buffer<int> buf = h.realize(200);

        for (int i = 0; i < 200; i++) {
            int correct = i < 100 ? 18701 : 345;
            if (buf(i) != correct) {
                printf("%d: buf(%d) = %d instead of %d\n", __LINE__, i, buf(i), correct);
                return -1;
            }
        }
    }

    if (1) {
        // In-place convolution. Shift the producer over a little to
        // avoid being clobbered by the consumer. This would write out
        // of bounds, so g can't be the output.
        Func f, g, h;
        Var x;
        f(x) = x;
        g(x) = f(x-1) + f(x) + f(x+1);
        h(x) = g(x);
        // If f is compute_root, then the realization of f is not
        // within the realization of g, so it's actually an
        // error. Need to add error checking, or place the realization
        // somewhere that includes both. Right now it just produced a
        // missing symbol error.
        f.compute_at(g, Var::outermost()).store_with(g, {x+1});
        g.compute_root();
        Buffer<int> buf = h.realize(100);
        for (int i = 0; i < 100; i++) {
            int correct = 3 * i;
            if (buf(i) != correct) {
                printf("%d: buf(%d) = %d instead of %d\n", __LINE__, i, buf(i), correct);
                return -1;
            }
        }
    }

    if (1) {
        // 2D in-place convolution.
        Func f, g, h;
        Var x, y;
        f(x, y) = x + y;
        g(x, y) = f(x-1, y-1) + f(x+1, y+1);
        h(x, y) = g(x, y);

        g.compute_root();
        // Computation of f must be nested inside computation of g
        f.compute_at(g, Var::outermost()).store_with(g, {x+1, y+1});
        Buffer<int> buf = h.realize(100, 100);

        for (int y = 0; y < 100; y++) {
            for (int x = 0; x < 100; x++) {
                int correct = 2*(x + y);
                if (buf(x, y) != correct) {
                    printf("%d: buf(%d, %d) = %d instead of %d\n", __LINE__, x, y, buf(x, y), correct);
                }
            }
        }
    }

    if (1) {
        // 2D in-place convolution computed per scanline
        Func f, g, h;
        Var x, y;
        f(x, y) = x + y;
        g(x, y) = f(x-1, y-1) + f(x+1, y+1);
        h(x, y) = g(x, y);

        g.compute_root();
        // Store slices of f two scanlines down in the as-yet-unused region of g
        f.compute_at(g, y).store_with(g, {x, y+2});
        Buffer<int> buf = h.realize(100, 100);

        for (int y = 0; y < 100; y++) {
            for (int x = 0; x < 100; x++) {
                int correct = 2*(x + y);
                if (buf(x, y) != correct) {
                    printf("%d: buf(%d, %d) = %d instead of %d\n", __LINE__, x, y, buf(x, y), correct);
                }
            }
        }
    }

    if (1) {
        // 2D in-place convolution computed per scanline with sliding
        Func f, g, h;
        Var x, y;
        f(x, y) = x + y;
        g(x, y) = f(x-1, y-1) + f(x+1, y+1);
        h(x, y) = g(x, y);

        g.compute_root();
        f.store_root().compute_at(g, y).store_with(g, {x, y+3});
        h.realize(100, 100);
    }

    if (1) {
        // split then merge
        Func f, g, h, out;
        Var x;
        f(x) = x;
        g(x) = f(2*x) + 1;
        h(x) = f(2*x+1) * 2;
        out(x) = select(x % 2 == 0, g(x/2), h(x/2));

        f.compute_root().store_with(out);
        g.compute_root().store_with(out, {2*x}); // Store g at the even spots in out
        h.compute_root().store_with(out, {2*x+1});  // Store h in the odd spots

        Buffer<int> buf = out.realize(100);

        for (int i = 0; i < 100; i++) {
            int correct = (i & 1) ? (i * 2) : (i + 1);
            if (buf(i) != correct) {
                printf("%d: buf(%d) = %d instead of %d\n", __LINE__, i, buf(i), correct);
                return -1;
            }
        }
    }

    if (1) {
        // split then merge, with parallelism
        Func f, g, h, out;
        Var x;
        f(x) = x;
        g(x) = f(2*x) + 1;
        h(x) = f(2*x+1) * 2;
        out(x) = select(x % 2 == 0, g(x/2), h(x/2));

        f.compute_root().vectorize(x, 8).store_with(out);
        // Store g at the even spots in out
        g.compute_root().vectorize(x, 8, TailStrategy::RoundUp).store_with(out, {2*x});
        // Store h in the odd spots
        h.compute_root().vectorize(x, 8, TailStrategy::RoundUp).store_with(out, {2*x+1});
        out.vectorize(x, 8, TailStrategy::RoundUp);

        Buffer<int> buf = out.realize(128);

        for (int i = 0; i < 100; i++) {
            int correct = (i & 1) ? (i * 2) : (i + 1);
            if (buf(i) != correct) {
                printf("buf(%d) = %d instead of %d\n", i, buf(i), correct);
                return -1;
            }
        }
    }

    if (1) {
        // A double integration in-place
        Func f, g, h;
        Var x;
        f(x) = x;
        RDom r(1, 99);
        g(x) = f(x);
        g(r) += g(r-1);
        h(x) = g(x);
        h(r) += h(r-1);

        f.compute_root().store_with(h);
        g.compute_root().store_with(h);
        h.bound(x, 0, 100);
        Buffer<int> buf = h.realize(100);

        for (int i = 0; i < 100; i++) {
            int correct = (i * (i + 1) * (i + 2)) / 6;
            if (buf(i) != correct) {
                printf("buf(%d) = %d instead of %d\n", i, buf(i), correct);
                return -1;
            }
        }
    }

    if (1) {
        // Something that only works because vector loop iterations
        // occur simultaneously, so stores from one lane definitely
        // aren't visible to others absent some other sequence point.
        Func f, g;
        Var x;
        f(x) = x;
        g(x) = f(31 - x);
        f.compute_root().store_with(g);
        g.bound(x, 0, 32).vectorize(x);
        Buffer<int> buf = g.realize(32);

        for (int i = 0; i < 32; i++) {
            int correct = 31 - i;
            if (buf(i) != correct) {
                printf("buf(%d) = %d instead of %d\n", i, buf(i), correct);
                return -1;
            }
        }
    }

    if (1) {
        // A tiled pyramid
        Func f, g, h;
        Var x, y;

        f(x, y) = x + y;

        g(x, y) = f(x/2, y/2) + 1;
        h(x, y) = g(x/2, y/2) + 2;

        // Store a 4x4 block of f densely in the top left of every 16x16 tile of h
        f.compute_at(h, Var::outermost())
            .store_with(h, {16*(x/4) + x%4, 16*(y/4) + y%4})
            .vectorize(x).unroll(y);

        // Store an 8x8 block of g similarly compacted in the bottom
        // right. It doesn't collide with f, and we're OK to overwrite
        // it when computing h because we compute h serially across y
        // and vectorized across x.
        g.compute_at(h, Var::outermost())
            .store_with(h, {16*(x/8) + x%8 + 8, 16*(y/8) + y%8 + 8})
            .vectorize(x).unroll(y);

        Var xi, yi;
        h.compute_at(h.in(), x).vectorize(x).unroll(y);
        h = h.in();
        h.align_bounds(x, 16).align_bounds(y, 16)
            .tile(x, y, xi, yi, 16, 16)
            .vectorize(xi).unroll(yi);

        Buffer<int> buf = h.realize(128, 128);

        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 128; x++) {
                int correct = x/4 + y/4 + 3;
                if (buf(x, y) != correct) {
                    printf("%d: buf(%d, %d) = %d instead of %d\n", __LINE__, x, y, buf(x, y), correct);
                    return -1;
                }
            }
        }
    }

    if (1) {
        // We can place the storage outside a parallel loop provided that there are no race conditions.
        Func f1, f2, g, h;
        Var x;
        RDom r(0, 100);
        f1(x) = x;
        f1(x) += r;
        f2(x) = x;
        // No race conditions on f2 because it's a race between atomic
        // stores of the same value. No race conditions on f1 because
        // distinct threads write to distinct sites.
        g(x) = f2(x - 1) + f2(x + 1) + f1(x);
        h(x) = g(x);

        Var xo, xi;
        g.compute_root().split(x, xo, xi, 16, TailStrategy::RoundUp).parallel(xo);
        f1.compute_at(g, xo).store_with(g, {x + 256});
        f2.compute_at(g, xo).store_with(g, {x + 512});
        h.bound(x, 0, 128);
        Buffer<int> buf = h.realize(128);

        for (int i = 0; i < 128; i++) {
            int correct = 4950 + i*3;
            if (buf(i) != correct) {
                printf("%d: buf(%d) = %d instead of %d\n", __LINE__, i, buf(i), correct);
                return -1;
            }
        }
    }

    if (1) {
        // Test mixing types and tuples (while preserving bit-widths)
        Func f, g, h;
        Var x;

        f(x) = {cast<uint8_t>(x), cast<float>(x)};
        g(x) = {cast<int8_t>(x), cast<uint32_t>(f(x)[0] + f(x)[1])};

        f.compute_root().store_with(g);

        Buffer<int8_t> b1(128);
        Buffer<uint32_t> b2(128);
        g.realize({b1, b2});

        // All of the types involved can store the numbers involved exactly.
        for (int i = 0; i < 128; i++) {
            int actual1 = (int)b1(i);
            int actual2 = (int)b2(i);
            int correct1 = i;
            int correct2 = 2*i;
            if (correct1 != actual1 || correct2 != actual2) {
                printf("%d: buf(%d) = {%d, %d} instead of {%d, %d}\n",
                       __LINE__, i, actual1, actual2, correct1, correct2);
                return -1;
            }
        }

    }

    // TODO: desirable extensions to store with:
    // - accommodate type or tuple dimensionality mismatches by adding new inner dimensions (e.g. widening downsamples in-place)
    // - the ability to store_with input buffers to express entire in-place pipelines
    // - the ability to store something in the unused bits of something else when we know Func value bounds

#ifdef WITH_EXCEPTIONS

#define ASSERT_UNREACHABLE do {printf("There was supposed to be an error before line %d\n", __LINE__); return -1;} while (0)

    const bool verbose = false;

    try {
        // Can't do in-place with shiftinwards tail strategies.
        Func f, g;
        Var x;
        f(x) = x;
        g(x) = f(x) + 3;
        f.compute_root().store_with(g);
        g.vectorize(x, 8, TailStrategy::ShiftInwards);
        g.compile_jit();
        ASSERT_UNREACHABLE;
    } catch (CompileError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    try {
        // Can't store_with the output in cases where it would grow the bounds of the output.
        Func f, g;
        Var x;
        f(x) = x;
        g(x) = f(x) + f(x+100);
        f.compute_root().store_with(g);
        g.realize(100);
        ASSERT_UNREACHABLE;
    } catch (RuntimeError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    try {
        // Don't clobber values we'll need later
        Func f, g, h;
        Var x;
        f(x) = x;
        g(x) = f(x-1) + f(x) + f(x+1);
        h(x) = g(x);
        f.compute_at(g, Var::outermost()).store_with(g);
        g.compute_root();
        h.compile_jit();
        ASSERT_UNREACHABLE;
    } catch (CompileError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    try {
        // Can't store multiple values at the same site
        Func f, g, h;
        Var x;
        f(x) = x;
        g(x) = f(x-1) + f(x) + f(x+1);
        h(x) = g(x);
        f.compute_at(g, Var::outermost()).store_with(g, {x/2 + 1000});
        g.compute_root().bound(x, 0, 100);
        h.compile_jit();
        ASSERT_UNREACHABLE;
    } catch (CompileError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    try {
        // Can't create race conditions by storing with something
        // outside a parallel loop and computing inside it.
        Func f, g, h;
        Var x;
        RDom r(0, 100);
        f(x) = x;
        f(x) += r;
        g(x) = f(x - 1) + f(x + 1);
        h(x) = g(x);

        Var xo, xi;
        g.compute_root().split(x, xo, xi, 16, TailStrategy::RoundUp).parallel(xo);
        f.compute_at(g, xo).store_with(g, {x + 256});
        h.bound(x, 0, 128);
        h.realize(128);
        ASSERT_UNREACHABLE;
    } catch (CompileError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    try {
        // Redundant recompute on the same memory is problematic even
        // without parallelism, if there are read-modify-writes.
        Func f, g, h;
        Var x;
        f(x) = x;
        RDom r(0, 256);
        f(r) += 1;
        g(x) = f(x);

        Var xo, xi;
        g.compute_root().split(x, xo, xi, 16, TailStrategy::RoundUp);
        f.compute_at(g, xo).store_with(g, {x + 256});
        g.bound(x, 0, 256);
        g.realize(256);
        ASSERT_UNREACHABLE;
    } catch (CompileError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    try {
        // Can't store_with inline things
        Func f, g, h;
        Var x;
        f(x) = x;
        g(x) = f(x);
        h(x) = g(x);

        f.compute_root().store_with(g); // g is inlined!
        h.realize(128);
        ASSERT_UNREACHABLE;
    } catch (CompileError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    try {
        // No transitive nonsense
        Func f, g, h;
        Var x;
        f(x) = x;
        g(x) = f(x);
        h(x) = g(x);

        f.compute_root().store_with(g);
        g.compute_root().store_with(h);
        h.realize(128);
        ASSERT_UNREACHABLE;
    } catch (CompileError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    try {
        // No storing with things not in the pipeline
        Func f, g, h;
        Var x;
        f(x) = x;
        g(x) = f(x);
        h(x) = f(x);

        f.compute_root().store_with(g);
        g.compute_root();
        // h has no dependence on g, so even though it's compute root,
        // it won't have a realization.
        h.realize(128);
        ASSERT_UNREACHABLE;
    } catch (CompileError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    try {
        // Can't currently mix tuple widths
        Func f, g, h;
        Var x;

        f(x) = {cast<uint8_t>(x), cast<float>(x)};
        g(x) = cast<uint32_t>(f(x)[0] + f(x)[1]);
        f.compute_root().store_with(g);
        g.realize(128);

        ASSERT_UNREACHABLE;
    } catch (CompileError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    try {
        // Can't currently mix bit widths
        Func f, g, h;
        Var x;

        f(x) = x;
        g(x) = cast<int64_t>(f(x));
        f.compute_root().store_with(g);
        g.realize(128);
        ASSERT_UNREACHABLE;
    } catch (CompileError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    try {
        // Dimensionality of placement site must match dimensionality of target Func
        Func f, g;
        Var x;

        f(x) = x;
        g(x) = f(x);
        f.compute_root().store_with(g, {x, 4});
        g.realize(128);
        ASSERT_UNREACHABLE;
    } catch (CompileError &e) {
        if (verbose) std::cerr << e.what() << "\n";
    }

    // TODO: async

#else
    printf("Not testing store_with failure cases because Halide was compiled without exceptions\n");
    return 0;
#endif

    if (0) {
        // An entire in-place pipeline
        ImageParam im(Float(32), 2);
        Func f;
        Var x, y;
        f(x, y) = im(x, y) + 17;

        // TODO: Add imageparam overloads
        f.store_with(im);
        f.realize(128);
    }

    printf("Success!\n");

    return 0;
}
