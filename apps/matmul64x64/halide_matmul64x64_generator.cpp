#include "Halide.h"

namespace {

class HalideMatMul64x64 : public Halide::Generator<HalideMatMul64x64> {
public:
    Input<Buffer<int8_t>> A{"A", 2};
    Input<Buffer<int8_t>> B{"B", 2};

    Output<Buffer<int16_t>> C{"C", 2};

    void generate() {
        Var x("x"), y("y"), xi("xi"), yi("yi"), xo("xo"), yo("yo"), xii("xii");
        RDom k(0, 64);
        RVar ki("ki");

        Func matmul("matmul");
        matmul(x, y) = cast(Int(24), 0);
        matmul(x, y) = matmul(x, y) 
                        + cast(Int(24), A(k, y)) * cast(Int(24), B(x, k));
                        // + cast(Int(24), A(4 * k + 1, y)) * cast(Int(24), B(x, 4 * k + 1))
                        // + cast(Int(24), A(4 * k + 2, y)) * cast(Int(24), B(x, 4 * k + 2))
                        // + cast(Int(24), A(4 * k + 3, y)) * cast(Int(24), B(x, 4 * k + 3));
        C(x,y) = cast(Int(16), matmul(x, y) >> 6); 


        if (get_target().has_feature(Target::Xtensa)) {
            C.split(y, yo, yi, 4)
             .vectorize(x, 64)
             .unroll(yi);
            
            matmul.compute_at(C, yo)
                .vectorize(x, 64)
                .unroll(y);

            matmul.update(0)
                .split(k, k, ki, 4)
                .reorder(x, ki, y, k)
                .vectorize(x, 64)
                .unroll(y)
                .unroll(k)
                .atomic()
                .vectorize(ki, 4)
                ;

            // A.in().compute_at(C, yo).vectorize(Halide::_0, 64).unroll(Halide::_1, 4);
        } else {
            // CPU schedule.
            C.vectorize(x, 8);
        }

        A.set_host_alignment(64);
        B.set_host_alignment(64);
        C.set_host_alignment(64);

        A.dim(0)
            .set_min(0)
            .set_extent((A.dim(0).extent() / 64) * 64);
        A.dim(1)
            .set_min(0);

        B.dim(0)
            .set_min(0)
            .set_extent((B.dim(0).extent() / 64) * 64);
        B.dim(1)
            .set_min(0);


        C.dim(0)
            .set_min(0)
            .set_extent((C.dim(0).extent() / 64) * 64);
        C.dim(1)
            .set_min(0);

        A.dim(1).set_stride(64);
        B.dim(1).set_stride(64);

        C.dim(1).set_stride(64);


        C.bound(x, 0, 64).bound(y, 0, 64);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(HalideMatMul64x64, halide_matmul64x64)
