// One mul_mat algorithm for every (weight, act) format pair:
//   out(n, m) = sum_k W(k, n) * X(k, m)  in f32,
// each operand approximated by its format's scheme, then severed at the
// encoded records, which become the inputs w: [K / block, N], a: [.., M].
#include "kernels/schedule.h"
#include "schemes/schemes.h"

namespace {

using namespace Halide;

class MatMul : public Generator<MatMul> {
public:
    GeneratorParam<std::string> weight{"weight", "q4_0"}, act{"act", "q8_0"};

    void configure() {
        ggml::Format fw = ggml::format(weight), fa = ggml::format(act);
        ImageParam w = input(fw, "w"), a = input(fa, "a");
        Var k("k"), n("n"), m("m");
        Func W("W"), X("X"), out("out");
        W(k, n) = operand(fw, w, k, n);
        X(k, m) = operand(fa, a, k, m);
        r = RDom(0, w.dim(0).extent() * fw.block, "r");
        out(n, m) = 0.0f;
        out(n, m) += W(r, n) * X(r, m);
        std::vector<Func> cut;
        std::vector<ImageParam> bound;
        for (auto [F, f, p] : {std::tuple{W, fw, w}, std::tuple{X, fa, a}}) {
            if (f.scheme.defined()) {
                approx.push_back(F.approximate_by(f.scheme, {out}));
                cut.push_back(approx.back().encoded[0]);
                bound.push_back(p);
            }
        }
        Pipeline(out).sever(cut, bound);
        add_input(w);
        add_input(a);
        result = add_output(out);
        blocks = {fw.block, fa.block};
    }

    void generate() {
        ggml::vec_dot(*result, r, blocks, approx, get_target());
    }

private:
    RDom r;
    std::vector<ApproximationResult> approx;
    std::vector<int> blocks;
    GeneratorOutput<Buffer<>> *result = nullptr;

    // The encoded records of a format (the values themselves for plain f32).
    static ImageParam input(const ggml::Format &f, const std::string &name) {
        Type t = f.scheme.defined() ? *f.scheme.signature({{"values", Float(32), 2}}).outputs[0].type : Float(32);
        return ImageParam(t, 2, name);
    }

    static Expr operand(const ggml::Format &f, const ImageParam &p, const Var &k, const Var &i) {
        return f.scheme.defined() ? ImageParam(Float(32), 2, p.name() + "_values")(k, i) : p(k, i);
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, matmul)
