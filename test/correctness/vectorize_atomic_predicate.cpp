#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam mat(Float(32), 2, "mat");
    mat.dim(0).set_min(0).set_extent(mat.dim(0).extent() / 4 * 4);
    mat.dim(1).set_min(0).set_stride(mat.dim(0).extent());

    ImageParam vec(Float(32), 1, "vec");
    vec.dim(0).set_bounds(0, mat.dim(0).extent());

    Func mv{"mv"};
    Var x{"x"};

    // RDom r(0, vec.dim(0).extent() / 4 * 4);  // <- works with this, because no tail
    RDom r(0, vec.dim(0).extent());
    mv(x) += mat(r, x) * vec(r);

    Func out = mv.in();

    RVar ro{"ro"}, ri{"ri"};
    Var u{"u"};

    out.output_buffer().dim(0).set_bounds(0, mat.dim(1).extent() / 4 * 4);
    out.vectorize(x, 4);

    auto intm = mv.update().split(r, ro, ri, 4, TailStrategy::Predicate).rfactor(ri, u);
    intm.compute_at(out, x)
        .reorder_storage(u, x)
        .vectorize(u)
        .unroll(x);

    intm.update().reorder(x, u, ro).vectorize(u).unroll(x);

    mv.update().atomic().vectorize(ri, 4);
    mv.bound_extent(x, 4);

    struct TestMatcher : Internal::IRVisitor {
        using IRVisitor::visit;
        void visit(const Internal::VectorReduce *op) override {
            found = true;
        }
        bool found = false;
    } matcher;

    auto stmt = out.compile_to_module(out.infer_arguments())
                    .get_conceptual_stmt();
    stmt.accept(&matcher);

    if (!matcher.found) {
        std::cout << "Did not find a VectorReduce node.\n";
        std::cout << stmt << "\n";
        return 1;
    }

    std::cout << "Success!\n";
    return 0;
}
