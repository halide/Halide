#include <Halide.h>
#include <stdio.h>
#include <math.h>

using std::vector;
using namespace Halide;
using namespace Halide::Internal;

class CountInterleaves : public IRVisitor {
public:
    int result;
    CountInterleaves() : result(0) {}

    using IRVisitor::visit;

    void visit(const Call *op) {
        if (op->name == Call::interleave_vectors &&
            op->call_type == Call::Intrinsic) {
            result++;
        }
        IRVisitor::visit(op);
    }
};

int count_interleaves(Func f) {
    Target t = get_jit_target_from_environment();
    t.set_feature(Target::NoBoundsQuery);
    t.set_feature(Target::NoAsserts);
    Stmt s = Internal::lower(f.function(), t);
    CountInterleaves i;
    s.accept(&i);
    return i.result;
}

void check_interleave_count(Func f, int correct) {
    int c = count_interleaves(f);
    if (c != correct) {
        printf("Func %s should have interleaved %d times but interleaved %d times instead.\n",
               f.name().c_str(), correct, c);
        exit(-1);
    }
}

// Make sure the interleave pattern generates good vector code

int main(int argc, char **argv) {
    Var x, y, c;

    {
        Func f, g, h;
        f(x) = sin(x);
        g(x) = cos(x);
        h(x) = select(x % 2 == 0, 1.0f/f(x/2), g(x/2)*17.0f);

        f.compute_root();
        g.compute_root();
        h.vectorize(x, 8);

        check_interleave_count(h, 1);

        Image<float> result = h.realize(16);
        for (int x = 0; x < 16; x++) {
            float correct = ((x % 2) == 0) ? (1.0f/(sinf(x/2))) : (cosf(x/2)*17.0f);
            float delta = result(x) - correct;
            if (delta > 0.01 || delta < -0.01) {
                printf("result(%d) = %f instead of %f\n", x, result(x), correct);
                return -1;
            }
        }
    }

    {
        // Test interleave 3 vectors:
        Func planar, interleaved;
        planar(x, y) = Halide::cast<float>( 3 * x + y );
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
            .set_min(1, 0)
            .set_stride(0, 3)
            .set_stride(1, 1)
            .set_extent(1, 3);

        Buffer buff3;
        buff3 = Buffer(Float(32), 16, 3, 0, 0, (uint8_t *)0);
        buff3.raw_buffer()->stride[0] = 3;
        buff3.raw_buffer()->stride[1] = 1;

        Realization r3(Internal::vec(buff3));
        interleaved.realize(r3);

        check_interleave_count(interleaved, 2);

        Image<float> result3 = r3[0];
        for (int x = 0; x < 16; x++) {
            for (int y = 0; y < 3; y++) {
                float correct = 3*x + y;
                float delta = result3(x,y) - correct;
                if (delta > 0.01 || delta < -0.01) {
                    printf("result(%d) = %f instead of %f\n", x, result3(x,y), correct);
                    return -1;
                }
            }
        }
    }

    {
        // Test interleave 4 vectors:
        Func f1, f2, f3, f4, f5;
        f1(x) = sin(x);
        f2(x) = sin(2*x);
        f3(x) = sin(3*x);
        f4(x) = sin(4*x);
        f5(x) = sin(5*x);

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
            .set_min(1, 0)
            .set_stride(0, 4)
            .set_stride(1, 1)
            .set_extent(1, 4);

        check_interleave_count(output4, 1);

        Buffer buff4;
        buff4 = Buffer(Float(32), 16, 4, 0, 0, (uint8_t *)0);
        buff4.raw_buffer()->stride[0] = 4;
        buff4.raw_buffer()->stride[1] = 1;

        Realization r4(Internal::vec(buff4));
        output4.realize(r4);

        Image<float> result4 = r4[0];
        for (int x = 0; x < 16; x++) {
            for (int y = 0; y < 4; y++) {
                float correct = sin((y+1)*x);
                float delta = result4(x,y) - correct;
                if (delta > 0.01 || delta < -0.01) {
                    printf("result(%d) = %f instead of %f\n", x, result4(x,y), correct);
                    return -1;
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
            .set_min(1, 0)
            .set_stride(0, 5)
            .set_stride(1, 1)
            .set_extent(1, 5);


        check_interleave_count(output5, 1);

        Buffer buff5;
        buff5 = Buffer(Float(32), 16, 5, 0, 0, (uint8_t *)0);
        buff5.raw_buffer()->stride[0] = 5;
        buff5.raw_buffer()->stride[1] = 1;

        Realization r5(Internal::vec(buff5));
        output5.realize(r5);

        Image<float> result5 = r5[0];
        for (int x = 0; x < 16; x++) {
            for (int y = 0; y < 5; y++) {
                float correct = sin((y+1)*x);
                float delta = result5(x,y) - correct;
                if (delta > 0.01 || delta < -0.01) {
                    printf("result(%d) = %f instead of %f\n", x, result5(x,y), correct);
                    return -1;
                }
            }
        }
    }

    {
        // Test interleaving inside of nested blocks
        Func f1, f2, f3, f4, f5;
        f1(x) = sin(x);
        f1.compute_root();

        f2(x) = sin(2*x);
        f2.compute_root();


        Func unrolled;
        unrolled(x, y) = select(x % 2 == 0, f1(x), f2(x)) + y;

        Var xi, yi;
        unrolled.tile(x, y, xi, yi, 2, 2).vectorize(x, 4).unroll(xi).unroll(yi).unroll(x, 2);

        check_interleave_count(unrolled, 4);
    }

    {
        // Make sure we don't interleave when the reordering would change the meaning.
        Image<uint8_t> ref;
        for (int i = 0; i < 2; i++) {
            Func output6;
            output6(x, y) = cast<uint8_t>(x);
            RDom r(0, 16);

            // A not-safe-to-merge pair of updates
            output6(2*r, 0) = cast<uint8_t>(3);
            output6(2*r+1, 0) = output6(2*r, 0)+2;

            // A safe-to-merge pair of updates
            output6(2*r, 1) = cast<uint8_t>(3);
            output6(2*r+1, 1) = cast<uint8_t>(4);

            // A safe-to-merge-but-not-complete triple of updates:
            output6(3*r, 3) = cast<uint8_t>(3);
            output6(3*r+1, 3) = cast<uint8_t>(4);

            // A safe-to-merge-but-we-don't pair of updates, because they
            // load recursively, so we conservatively bail out.
            output6(2*r, 2) += 1;
            output6(2*r+1, 2) += 2;

            // A safe-to-merge triple of updates:
            output6(3*r, 3) = cast<uint8_t>(7);
            output6(3*r+2, 3) = cast<uint8_t>(9);
            output6(3*r+1, 3) = cast<uint8_t>(8);

            if (i == 0) {
                // Making the reference output.
                ref = output6.realize(50, 4);
            } else {
                // Vectorize and compare to the reference.
                for (int j = 0; j < 11; j++) {
                    output6.update(j).vectorize(r);
                }

                check_interleave_count(output6, 2);

                Image<uint8_t> out = output6.realize(50, 4);
                for (int y = 0; y < ref.height(); y++) {
                    for (int x = 0; x < ref.width(); x++) {
                        if (out(x, y) != ref(x, y)) {
                            printf("result(%d, %d) = %d instead of %d\n",
                                   x, y, out(x, y), ref(x, y));
                            return -1;
                        }
                    }
                }
            }
        }
    }

    {
        // Test that transposition works when vectorizing either dimension:
        Func square("square");
        square(x, y) = cast(UInt(16), 5*x + y);

        Func trans1("trans1");
        trans1(x, y) = square(y, x);

        Func trans2("trans2");
        trans2(x, y) = square(y, x);

        square.compute_root()
            .bound(x, 0, 8)
            .bound(y, 0, 8);

        trans1.compute_root()
            .bound(x, 0, 8)
            .bound(y, 0, 8)
            .vectorize(x)
            .unroll(y);

        trans2.compute_root()
            .bound(x, 0, 8)
            .bound(y, 0, 8)
            .unroll(x)
            .vectorize(y);

        trans1.output_buffer()
            .set_min(0,0).set_min(1,0)
            .set_stride(0,1).set_stride(1,8)
            .set_extent(0,8).set_extent(1,8);

        trans2.output_buffer()
            .set_min(0,0).set_min(1,0)
            .set_stride(0,1).set_stride(1,8)
            .set_extent(0,8).set_extent(1,8);

        Image<uint16_t> result6(8,8);
        Image<uint16_t> result7(8,8);
        trans1.realize(result6);
        trans2.realize(result7);

        for (int x = 0; x < 8; x++) {
            for (int y = 0; y < 8; y++) {
                int correct = 5*y + x;
                if (result6(x,y) != correct) {
                    printf("result(%d) = %d instead of %d\n", x, result6(x,y), correct);
                    return -1;
                }

                if (result7(x,y) != correct) {
                    printf("result(%d) = %d instead of %d\n", x, result7(x,y), correct);
                    return -1;
                }
            }
        }

        check_interleave_count(trans1, 1);
        check_interleave_count(trans2, 1);
    }

    printf("Success!\n");
    return 0;
}
