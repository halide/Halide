// One mul_mat algorithm for every (weight, act) format pair:
//   out(n, m) = sum_k W(k, n) * X(k, m)  in f32,
// each operand approximated by its format's scheme, then severed at the
// encoded records, which become the inputs w: [K / block, N], a: [.., M].
// act = "<storage>[:<compute>]": X arrives as <storage> and, given a compute
// format, is approximated by it inside the pipeline too (not severed), as
// GGML quantizes f32 activations.
#include "kernels/schedule.h"
#include "schemes/schemes.h"

namespace {

using namespace Halide;

class MatMul : public Generator<MatMul> {
public:
    GeneratorParam<std::string> weight{"weight", "q4_0"}, act{"act", "q8_0"}, op{"op", "vec_dot"};

    void configure() {
        std::string an = act, cn = an.substr(an.find(':') + 1);
        an = an.substr(0, an.find(':'));
        ggml::Format fw = ggml::format(weight), fa = ggml::format(an), fc = ggml::format(cn);
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
        if (cn != an) {
            if (fa.scheme.defined()) throw std::invalid_argument("act: only plain storage can be re-approximated");
            approx.push_back(X.approximate_by(fc.scheme, {out}));
            staged.push_back(approx.back().encoded[0]);
        }
        Pipeline(out).sever(cut, bound);
        // The ABI: w [K / w block, N], a [K / a block, M], out [N, M], all
        // dense from 0 (strides as GGML's contiguous rows; K % block == 0).
        if (fw.block % fa.block || fw.block % fc.block) throw std::invalid_argument("activation blocks must divide weight blocks");
        OutputImageParam o = out.output_buffer();
        o.dim(0).set_min(0).dim(1).set_min(0).set_stride(o.dim(0).extent());
        w.dim(0).set_min(0).dim(1).set_bounds(0, o.dim(0).extent()).set_stride(w.dim(0).extent());
        a.dim(0).set_bounds(0, w.dim(0).extent() * (fw.block / fa.block));
        a.dim(1).set_bounds(0, o.dim(1).extent()).set_stride(a.dim(0).extent());
        add_input(w);
        add_input(a);
        result = add_output(out);
        blocks = {fw.block, fc.block};
    }

    void generate() {
        (op == "vec_dot" ? ggml::vec_dot : ggml::mul_mat)(*result, r, blocks, approx, staged, get_target());
    }

private:
    RDom r;
    std::vector<ApproximationResult> approx;
    std::vector<Func> staged;  // encoded inside the pipeline
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
