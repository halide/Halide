// One mul_mat algorithm for every (weight, act) format pair:
//   out(n, m) = sum_k W(k, n) * X(k, m)  in f32,
// (the weight format's `acc`, if any, then approximates the sum's partial
// sums in the schedule: e.g. f16.acc16, in f16 as GGML's f16 vec_dot),
// each operand approximated by its format's scheme, then severed at the
// encoded records, which become the inputs w: [K / block, N / rows], a: [.., M]
// (one input per encoded port, e.g. per field of a planar layout, its
// elements first: [elements, K / block, N]).
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
    // vec_dot's blocks per iteration (ggml::vec_dot)
    GeneratorParam<int> interleave{"interleave", 2};

    void configure() {
        std::string an = act, cn = an.substr(an.find(':') + 1);
        an = an.substr(0, an.find(':'));
        // mul_mat on weight records of several rows takes their codes at 2^k
        // (an exact alternative, see Type0Quant): it shortens the gemv's decode
        bool scaled = op == "mul_mat" && ggml::format(weight).rows > 1;
        bool tables = ggml::use_tables(get_target());
        ggml::Format fw = ggml::format(weight, false, scaled, true, Expr(), tables), fa = ggml::format(an, false, false, true), fc = ggml::format(cn);
        std::vector<ImageParam> w = inputs(fw, "w"), a = inputs(fa, "a");
        if (fw.rowwise || fa.rowwise || fc.rowwise) {
            // Rowwise formats reduce over a row's blocks: the weight's records
            Expr nb = record(w[0]).extent();
            fw = ggml::format(weight, false, scaled, true, nb, tables), fa = ggml::format(an, false, false, true, nb), fc = ggml::format(cn, false, false, false, nb);
            if (!fw.rowwise || !(fa.rowwise || fc.rowwise)) throw std::invalid_argument("rowwise formats: both operands'");
        }
        Var k("k"), n("n"), m("m");
        Func Wf("Wf"), Xf("Xf"), Xrf("Xrf"), W("W"), X("X"), Xr("Xr"), out("out");
        // The reduction's blocks: the weight's, or an act's blocks dividing
        // them (nvfp4, q1_0 with q8_0 acts): each weight block then spans
        // `per` of the reduction's
        int ab = std::max(fa.block, fc.block);
        wblock = fw.block;
        block = ab > 1 && ab < wblock && wblock % ab == 0 ? ab : wblock;
        const int per = wblock / block;
        Wf(k, n) = operand(fw, w, k, n);
        // a rowwise act encoded here may be encoded on rows past M (whole
        // groups of rows for the gemm's tiles): they repeat the last one
        Xf(k, m) = operand(fa, a, k, fc.rowwise && cn != an ? clamp(m, 0, row(a[0]).extent() - 1) : m);
        view(W, Wf, wblock);
        view(X, Xf, block);
        // r.x: a value within a block, r.y: the block
        r = RDom(0, block, 0, record(w[0]).extent() * per, "r");
        Expr wj = per > 1 ? r.x + r.y % per * block : r.x, wb = per > 1 ? r.y / per : r.y;
        dots[0](n, m) += W(wj, wb, n) * X(r.x, r.y, m);  // from 0; out writes each output once
        if (fc.rows > 1) {
            // Act records of several rows (as GGML's x4 activations): the
            // rows in whole records from them (dots[1]; rows past M repeat the
            // last one), the others one row per record.
            Expr M = row(a[0]).extent(), Mr = M / fc.rows * fc.rows;
            Xrf(k, m) = operand(fa, a, k, clamp(m, 0, M - 1));
            view(Xr, Xrf, block);
            dots.push_back(Func("dotr"));
            dots[1](n, m) += W(wj, wb, n) * Xr(r.x, r.y, m);
            RDom whole(0, Mr, "whole"), rest(Mr, M - Mr, "rest");
            out(n, m) = undef<float>();
            out(n, whole) = dots[1](n, whole);
            out(n, rest) = dots[0](n, rest);
        } else {
            out(n, m) = dots[0](n, m);
        }
        std::vector<Func> cut;
        std::vector<ImageParam> bound;
        // approx[0] is the weight's (the schedules rely on it)
        if (!fw.scheme.defined()) throw std::invalid_argument("weight: a quantized format");
        for (auto [F, Ff, f, p] : {std::tuple{W, Wf, fw, w}, std::tuple{X, Xf, fa, a}}) {
            if (f.scheme.defined()) {
                approx.push_back(approximate(F, Ff, f, dots, F.name() == W.name() ? wblock : block));
                cut.insert(cut.end(), approx.back().encoded.begin(), approx.back().encoded.end());
                bound.insert(bound.end(), p.begin(), p.end());
            }
        }
        if (cn != an) {
            if (fa.scheme.defined()) throw std::invalid_argument("act: only plain storage can be re-approximated");
            for (size_t i = 0; i < dots.size(); i++) {
                approx.push_back(approximate(i ? Xr : X, i ? Xrf : Xf, ggml::format(cn, !i, false, true, fc.rowwise ? record(w[0]).extent() : Expr()), {dots[i]}, block));
                staged.push_back(approx.back().encoded[0]);
            }
        }
        // Derived ports (e.g. q8_1's block sums) are dead in the decode until a
        // schedule reads them instead of a term they equal: sever them too.
        std::vector<Func> outs{out};
        for (const ApproximationResult &res : approx) {
            for (const DerivedPort &p : res.derived_ports()) {
                outs.push_back(p.stored);
            }
        }
        Pipeline(outs).sever(cut, bound);
        // The schedules name an operand's Funcs by its result: its severed
        // ports are then the stand-ins calling their ImageParams.
        std::vector<Internal::Function> fs;
        for (const Func &f : outs) {
            fs.push_back(f.function());
        }
        std::map<std::string, Func> stand_in;
        for (const auto &[name, f] : Internal::build_environment(fs)) {
            const Internal::Call *c = f.is_wrapper();
            for (size_t i = 0; c && i < cut.size(); i++) {
                if (c->name == Func(bound[i]).name()) stand_in.emplace(cut[i].name(), Func(f));
            }
        }
        for (ApproximationResult &res : approx) {
            for (Func &e : res.encoded) {
                if (stand_in.count(e.name())) e = stand_in.at(e.name());
            }
        }
        // The ABI: w [K / w block, N / w rows], a [K / a block, M], out [N, M],
        // all dense from 0 (strides as GGML's contiguous rows; K % block == 0),
        // a field's elements first; a rowwise format's per-row ports [N] or [M].
        for (int b : {fa.block, fc.block}) {
            if (b != 1 && b != block) throw std::invalid_argument("activation blocks: the weight's, ones dividing them, or none");
        }
        if (fa.rows > 1) throw std::invalid_argument("act: one row per record");
        OutputImageParam o = out.output_buffer();
        o.dim(0).set_bounds(0, row(w[0]).extent() * fw.rows).dim(1).set_min(0).set_stride(o.dim(0).extent());
        for (ImageParam p : w) {
            dense(p);
            if (p.name() == w[0].name()) continue;
            if (p.dimensions() == 1) {
                row(p).set_extent(o.dim(0).extent());
                continue;
            }
            record(p).set_extent(record(w[0]).extent());
            row(p).set_extent(row(w[0]).extent());
        }
        for (ImageParam p : a) {
            dense(p);
            if (p.dimensions() > 1) record(p).set_extent(record(w[0]).extent() * (fw.block / fa.block));
            row(p).set_extent(o.dim(1).extent());
        }
        for (ImageParam p : w) {
            add_input(p);
        }
        for (ImageParam p : a) {
            add_input(p);
        }
        result = add_output(out);
        fmt = {block, fw.rows, fw.chunk, fc.block, fc.rows, fw.rowwise, fw.sub, fc.sub};
        acc = fw.acc;
        // The accumulation's order is part of its numerics: without
        // strict_float, LLVM may reassociate the partial sums' tree
        if (acc.defined() && !get_target().has_feature(Target::StrictFloat)) {
            throw std::invalid_argument("matmul: weight " + std::string(weight) + " (an accumulation order) needs the strict_float target feature");
        }
    }

    void generate() {
        if (op == "vec_dot") {
            ggml::vec_dot(*result, dots, r, fmt, approx, staged, get_target(), acc, interleave);
        } else {
            ggml::mul_mat(*result, dots, r, fmt, approx, staged, get_target(), acc);
        }
    }

private:
    RDom r;
    int block = 1, wblock = 1;                // the reduction's blocks, the weight's
    std::vector<Func> dots{Func("dot")};      // one per act encoding
    std::vector<ApproximationResult> approx;  // the weight's first (asserted)
    std::vector<Func> staged;                 // encoded inside the pipeline
    ggml::Formats fmt;
    Approximation acc;  // the dot's sums (undefined: f32)
    GeneratorOutput<Buffer<>> *result = nullptr;

    // The encoded ports of a format (the values themselves for plain f32);
    // `name`, or name_<port> for several.
    static std::vector<ImageParam> inputs(const ggml::Format &f, const std::string &name) {
        if (!f.scheme.defined()) return {ImageParam(Float(32), 2, name)};
        int dims = f.block > 1 ? 3 : 2;  // blocks: indexed (j, b, row)
        ApproximationPorts out = f.scheme.signature({{"values", Float(32), dims}}).outputs;
        if (out.size() > 1) {  // their shapes: encode a placeholder
            Func v;
            v(std::vector<Var>(dims)) = 0.0f;
            out = f.scheme.encode({v}).encoded_ports;
        }
        std::vector<ImageParam> ps;
        for (const ApproximationPort &p : out) {
            ps.emplace_back(*p.type, p.dimensions.value_or(2), out.size() > 1 ? name + "_" + p.name : name);
        }
        return ps;
    }
    static Internal::Dimension record(const ImageParam &p) {
        return p.dim(p.dimensions() - 2);
    }
    static Internal::Dimension row(const ImageParam &p) {
        return p.dim(p.dimensions() - 1);
    }
    static void dense(const ImageParam &p) {
        Expr stride;
        for (int i = 0; i < p.dimensions(); i++) {
            p.dim(i).set_min(0);
            if (i) p.dim(i).set_stride(stride);
            stride = i ? stride * p.dim(i).extent() : p.dim(i).extent();
        }
    }

    static Expr operand(const ggml::Format &f, const std::vector<ImageParam> &p, const Expr &k, const Expr &i) {
        return f.scheme.defined() ? ImageParam(Float(32), 2, p[0].name() + "_values")(k, i) : p[0](k, i);
    }
    // F(j, b, i): the flat operand's value j of block b (of n values: the
    // weight's or the reduction's), as the reduction reads it, so no index
    // into a block is a % or / of r.x.
    static void view(Func F, Func flat, int n) {
        Var j("j"), b("b"), i("i");
        F(j, b, i) = flat(b * n + j, i);
    }
    // A format in its view's blocks (of n values) approximates the view
    // (block-indexed); one without blocks, the flat rows.
    static ApproximationResult approximate(Func F, Func flat, const ggml::Format &f, const std::vector<Func> &consumers, int n) {
        return f.block == n && n > 1 ? F.approximate_by(f.scheme, consumers) : flat.approximate_by(f.scheme, {F});
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(MatMul, matmul)
