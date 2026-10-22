#include "Halide.h"

#include <cstdio>
#include <functional>
#include <random>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Look for vector values in the producer of a Func vectorized with
// VectorizeIn::GPR. The only vectors allowed are dense loads and stores of
// words, which appear as reinterprets between a vector load or store and
// a scalar word.
class FindVectors : public IRVisitor {
    using IRVisitor::visit;

    const std::string &func;
    const bool verbose;
    int in_producer = 0;

    void bad(const Expr &e) {
        if (in_producer && e.type().is_vector()) {
            if (verbose && vectors.empty()) {
                std::cout << "Found vector expression in producer of " << func << ":\n  " << e << "\n";
            }
            vectors.push_back(e);
        }
    }

    void visit_dense_index(const Expr &index, const Expr &predicate) {
        const Ramp *r = index.as<Ramp>();
        if (!r || !is_const_one(r->stride) || !r->base.type().is_scalar() || !is_const_one(predicate)) {
            bad(index);
        } else {
            r->base.accept(this);
        }
    }

    void visit(const ProducerConsumer *op) override {
        if (op->is_producer && op->name == func) {
            in_producer++;
            found_producer = true;
            IRVisitor::visit(op);
            in_producer--;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Reinterpret *op) override {
        if (const Load *l = op->value.as<Load>(); l && op->type.is_scalar()) {
            visit_dense_index(l->index, l->predicate);
        } else {
            bad(op);
            IRVisitor::visit(op);
        }
    }

    void visit(const Store *op) override {
        if (!in_producer || op->value.type().is_scalar()) {
            IRVisitor::visit(op);
            return;
        }
        // Common subexpression elimination may have wrapped lets around
        // the stored value.
        Expr value = op->value;
        while (const Let *l = value.as<Let>()) {
            l->value.accept(this);
            value = l->body;
        }
        const Reinterpret *r = value.as<Reinterpret>();
        if (r && r->value.type().is_scalar()) {
            r->value.accept(this);
            visit_dense_index(op->index, op->predicate);
        } else {
            bad(op->value);
            IRVisitor::visit(op);
        }
    }

#define CHECK(T)                       \
    void visit(const T *op) override { \
        bad(op);                       \
        IRVisitor::visit(op);          \
    }
    CHECK(Load)
    CHECK(Broadcast)
    CHECK(Ramp)
    CHECK(Add)
    CHECK(Sub)
    CHECK(Mul)
    CHECK(Div)
    CHECK(Mod)
    CHECK(Min)
    CHECK(Max)
    CHECK(Select)
    CHECK(Cast)
    CHECK(Call)
    CHECK(Shuffle)
    CHECK(Variable)
    CHECK(Let)
    CHECK(VectorReduce)
    CHECK(EQ)
    CHECK(NE)
    CHECK(LT)
    CHECK(LE)
    CHECK(GT)
    CHECK(GE)
    CHECK(And)
    CHECK(Or)
    CHECK(Not)
#undef CHECK

public:
    std::vector<Expr> vectors;
    bool found_producer = false;

    FindVectors(const std::string &func, bool verbose)
        : func(func), verbose(verbose) {
    }
};

class FindVectorsPass : public IRMutator {
public:
    std::vector<std::string> funcs;
    size_t num_gpr_funcs;
    std::vector<bool> found_producer, has_vectors;

    using IRMutator::mutate;
    Stmt mutate(const Stmt &s) override {
        found_producer.clear();
        has_vectors.clear();
        for (size_t i = 0; i < funcs.size(); i++) {
            FindVectors finder(funcs[i], i < num_gpr_funcs);
            s.accept(&finder);
            found_producer.push_back(finder.found_producer);
            has_vectors.push_back(!finder.vectors.empty());
        }
        return s;
    }

    FindVectorsPass(const std::vector<std::string> &funcs, size_t num_gpr_funcs)
        : funcs(funcs), num_gpr_funcs(num_gpr_funcs) {
    }
};

// Compile the pipeline, checking that the producers of the gpr_funcs have
// no vectors left in them, and that the producers of the simd_funcs do
// (i.e. that they were not lowered to general-purpose registers).
bool check_vectors(Pipeline p, const std::vector<Func> &gpr_funcs,
                   const std::vector<Func> &simd_funcs = {}) {
    std::vector<std::string> names;
    for (const Func &f : gpr_funcs) {
        names.push_back(f.name());
    }
    for (const Func &f : simd_funcs) {
        names.push_back(f.name());
    }
    FindVectorsPass checker(names, gpr_funcs.size());
    p.add_custom_lowering_pass(&checker, nullptr);
    p.compile_jit();
    p.clear_custom_lowering_passes();
    bool ok = checker.found_producer.size() == names.size();
    for (size_t i = 0; ok && i < names.size(); i++) {
        const bool gpr = i < gpr_funcs.size();
        if (!checker.found_producer[i]) {
            printf("Did not find the producer of %s\n", names[i].c_str());
            ok = false;
        } else if (gpr && checker.has_vectors[i]) {
            printf("GPR-vectorized Func %s still contains vectors\n", names[i].c_str());
            ok = false;
        } else if (!gpr && !checker.has_vectors[i]) {
            printf("SIMD-vectorized Func %s contains no vectors\n", names[i].c_str());
            ok = false;
        }
    }
    return ok;
}

bool check_no_vectors(Pipeline p, const Func &gpr_func) {
    return check_vectors(std::move(p), {gpr_func});
}

std::mt19937 rng(0x5eed);

Param<int> uniform_cond("uniform_cond");
Param<uint8_t> uniform_shift("uniform_shift");
Param<int8_t> uniform_signed_shift("uniform_signed_shift");
Param<uint8_t> uniform_value("uniform_value");

template<typename T>
Buffer<T> random_buffer(const std::vector<int> &sizes) {
    Buffer<T> buf(sizes);
    int i = 0;
    buf.for_each_value([&](T &v) {
        // Make sure the extreme values show up.
        switch (i++ % 7) {
        case 0:
            v = 0;
            break;
        case 3:
            v = (T) ~(T)0;
            break;
        default:
            v = (T)rng();
        }
    });
    return buf;
}

template<typename T>
bool check_equal(const char *name, const Buffer<T> &out, const Buffer<T> &ref) {
    bool ok = true;
    out.for_each_element([&](const int *pos) {
        T a = out(pos), b = ref(pos);
        if (ok && a != b) {
            printf("%s: mismatch at (%d, %d): got %llu, expected %llu\n",
                   name, pos[0], out.dimensions() > 1 ? pos[1] : 0,
                   (unsigned long long)a, (unsigned long long)b);
            ok = false;
        }
    });
    return ok;
}

// GGML's q4_K/q5_K 6-bit scales and mins: the 12 packed bytes of each
// block decode to 8 scales then 8 mins.
Expr kscales_decode(Func in_f, Var k, Var b) {
    auto byte = [&](const Expr &idx) { return in_f(idx, b); };
    auto u8 = [](const Expr &v) { return cast<uint8_t>(v); };
    Expr s = k % 8, m = k / 8, lo = s < 4;
    Expr x = select(lo, byte(s + 4 * m), byte(s + 4)), y = byte(s % 4 + 4 * m);
    Expr shift = u8(select(lo, 0, 4 * m)), mask = u8(select(lo, 63, 15)), top = u8(select(lo, 0, 48));
    return ((x >> shift) & mask) | ((y >> 2) & top);
}

Buffer<uint8_t> kscales_reference(const Buffer<uint8_t> &in) {
    Buffer<uint8_t> ref(16, in.dim(1).extent());
    for (int b = 0; b < in.dim(1).extent(); b++) {
        auto q = [&](int i) { return in(i, b); };
        for (int j = 0; j < 8; j++) {
            uint8_t d, m;
            if (j < 4) {
                d = q(j) & 63;
                m = q(j + 4) & 63;
            } else {
                d = (q(j + 4) & 0xF) | ((q(j - 4) >> 6) << 4);
                m = (q(j + 4) >> 4) | ((q(j) >> 6) << 4);
            }
            ref(j, b) = d;
            ref(j + 8, b) = m;
        }
    }
    return ref;
}

bool test_kscales(int lanes) {
    const int nb = 37;
    Buffer<uint8_t> in = random_buffer<uint8_t>({12, nb});

    Func in_f("in_f"), f("kscales");
    Var k("k"), b("b"), j("j"), i("i");
    in_f(i, b) = in(i, b);
    f(k, b) = kscales_decode(in_f, k, b);
    f.bound(k, 0, 16).split(k, j, i, lanes).unroll(j).vectorize(i, lanes, VectorizeIn::GPR);

    if (!check_no_vectors(Pipeline(f), f)) {
        return false;
    }
    Buffer<uint8_t> out = f.realize({16, nb});
    return check_equal("kscales", out, kscales_reference(in));
}

// A GPR producer computed per block inside a SIMD consumer, which is how
// the scales get used in the q4_K dot product.
bool test_kscales_consumer(bool register_storage) {
    const int nb = 23;
    Buffer<uint8_t> in = random_buffer<uint8_t>({12, nb});
    Buffer<int16_t> bs = random_buffer<int16_t>({8, nb});

    Func in_f("in_f"), f("kscales"), g("g");
    Var k("k"), b("b"), j("j"), i("i");
    in_f(i, b) = in(i, b);
    f(k, b) = kscales_decode(in_f, k, b);
    g(k, b) = cast<int32_t>(f(k + 8, b)) * cast<int32_t>(bs(k, b));

    g.bound(k, 0, 8).vectorize(k, 8);
    f.compute_at(g, b).bound(k, 0, 16).split(k, j, i, 4).unroll(j).vectorize(i, 4, VectorizeIn::GPR);
    if (register_storage) {
        f.store_in(MemoryType::Register);
    }

    if (!check_vectors(Pipeline(g), {f}, {g})) {
        return false;
    }
    Buffer<int32_t> out = g.realize({8, nb});
    Buffer<uint8_t> scales = kscales_reference(in);
    Buffer<int32_t> ref(8, nb);
    for (int bb = 0; bb < nb; bb++) {
        for (int kk = 0; kk < 8; kk++) {
            ref(kk, bb) = (int32_t)scales(kk + 8, bb) * (int32_t)bs(kk, bb);
        }
    }
    return check_equal("kscales_consumer", out, ref);
}

// The same, but with the decode in a wrapper of an inlined Func, computed
// at an RVar of the update of a SIMD-vectorized accumulator.
bool test_kscales_wrapper() {
    const int nb = 19;
    Buffer<uint8_t> in = random_buffer<uint8_t>({12, nb});
    Buffer<int16_t> bs = random_buffer<int16_t>({8, nb});

    Func in_f("in_f"), f("kscales"), acc("acc");
    Var k("k"), b("b"), j("j"), i("i");
    RDom r(0, nb, "r");
    in_f(i, b) = in(i, b);
    f(k, b) = kscales_decode(in_f, k, b);
    acc(k) = 0;
    acc(k) += cast<int32_t>(f(k, r)) * cast<int32_t>(f(k + 8, r)) * cast<int32_t>(bs(k, r));

    acc.bound(k, 0, 8).vectorize(k, 8);
    acc.update().reorder(k, r).vectorize(k, 8);
    Func wrapper = f.in(acc);
    wrapper.compute_at(acc, r).split(k, j, i, 4).unroll(j).vectorize(i, 4, VectorizeIn::GPR);

    if (!check_vectors(Pipeline(acc), {wrapper}, {acc})) {
        return false;
    }
    Buffer<int32_t> out = acc.realize({8});
    Buffer<uint8_t> scales = kscales_reference(in);
    Buffer<int32_t> ref(8);
    for (int kk = 0; kk < 8; kk++) {
        int32_t sum = 0;
        for (int bb = 0; bb < nb; bb++) {
            sum += (int32_t)scales(kk, bb) * (int32_t)scales(kk + 8, bb) * (int32_t)bs(kk, bb);
        }
        ref(kk) = sum;
    }
    return check_equal("kscales_wrapper", out, ref);
}

// Update stages can be vectorized with VectorizeIn::GPR too, while a
// sibling Func computed in the same loop nest stays in SIMD registers.
bool test_update_and_sibling() {
    const int nb = 29;
    Buffer<uint8_t> in = random_buffer<uint8_t>({12, nb});
    Buffer<uint8_t> in2 = random_buffer<uint8_t>({16, nb});

    Func in_f("in_f"), f("kscales"), s("simd_sibling"), out("out");
    Var k("k"), b("b"), j("j"), i("i");
    RDom r(0, 16, "r");
    in_f(i, b) = in(i, b);
    f(k, b) = kscales_decode(in_f, k, b);
    // An update with pure vars, and one with an RVar.
    f(k, b) = f(k, b) ^ (f(k, b) >> 3);
    f(r, b) = f(r, b) + cast<uint8_t>(r * 5);
    // Multiplication by a non-power-of-two can't be done in a
    // general-purpose register, so this would fail if it weren't SIMD.
    // (A dense load: on SVE2, LLVM 21 aborts with "Unable to widen vector
    // load" on the <vscale x 3 x i32> load Halide makes for in(k % 12, b).)
    s(k, b) = in2(k, b) * (uint8_t)3;
    out(k, b) = f(k, b) - s(k, b);

    out.bound(k, 0, 16);
    f.compute_at(out, b).split(k, j, i, 4).unroll(j).vectorize(i, 4, VectorizeIn::GPR);
    f.update(0).vectorize(k, 4, VectorizeIn::GPR);
    f.update(1).vectorize(r, 4, VectorizeIn::GPR);
    s.compute_at(out, b).vectorize(k, 16);

    if (!check_vectors(Pipeline(out), {f}, {s})) {
        return false;
    }
    Buffer<uint8_t> result = out.realize({16, nb});
    Buffer<uint8_t> scales = kscales_reference(in);
    Buffer<uint8_t> ref(16, nb);
    for (int bb = 0; bb < nb; bb++) {
        for (int kk = 0; kk < 16; kk++) {
            uint8_t v = scales(kk, bb);
            v ^= v >> 3;
            v += (uint8_t)(kk * 5);
            ref(kk, bb) = v - (uint8_t)(in2(kk, bb) * 3);
        }
    }
    return check_equal("update_and_sibling", result, ref);
}

// Check a GPR-vectorized Func against the same Func computed without
// vectorization. The definition is given the Var to vectorize and the
// inputs.
template<typename T>
bool test_elementwise(const char *name, int lanes,
                      std::function<Expr(Var, Func, Func)> def,
                      int size = 64,
                      TailStrategy tail = TailStrategy::Auto) {
    // Leave room for tail strategies that read past the end of the output.
    Buffer<T> a_buf = random_buffer<T>({size + 16}), c_buf = random_buffer<T>({size + 16});
    Var x("x");
    Func a("a"), c("c");
    a(x) = a_buf(x);
    c(x) = c_buf(x);

    Func f(std::string(name) + "_gpr"), ref(std::string(name) + "_ref");
    f(x) = def(x, a, c);
    ref(x) = def(x, a, c);
    f.vectorize(x, lanes, VectorizeIn::GPR, tail);

    if (!check_no_vectors(Pipeline(f), f)) {
        printf("  in test %s with %d lanes\n", name, lanes);
        return false;
    }
    Buffer<T> out = f.realize({size});
    Buffer<T> expected = ref.realize({size});
    if (!check_equal(name, out, expected)) {
        printf("  in test %s with %d lanes\n", name, lanes);
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (t.has_gpu_feature()) {
        // The Funcs below all run on the CPU anyway.
        t = t.without_feature(Target::CUDA).without_feature(Target::OpenCL).without_feature(Target::Metal);
    }
    const int word_bits = t.bits;
    uniform_value.set(0x93);
    uniform_cond.set(1);
    uniform_shift.set(3);
    uniform_signed_shift.set(2);

    bool ok = true;

    // KScales decode, one 32-bit word per unrolled iteration.
    ok = ok && test_kscales(4);
    if (word_bits >= 64) {
        // Eight lanes per word: shift amounts, masks, and the choice of
        // which bytes to load differ between the two halves of each word.
        ok = ok && test_kscales(8);
    }
    ok = ok && test_kscales_consumer(false);
    ok = ok && test_kscales_consumer(true);
    ok = ok && test_kscales_wrapper();
    ok = ok && test_update_and_sibling();

    std::vector<int> u8_lanes = {2, 4};
    std::vector<int> u16_lanes = {2};
    if (word_bits >= 64) {
        u8_lanes.push_back(8);
        u16_lanes.push_back(4);
    }

    for (int lanes : u8_lanes) {
        // Lane-wise adds and subtracts, which must not carry or borrow into
        // the neighboring lane.
        ok = ok && test_elementwise<uint8_t>("add", lanes, [](Var x, Func a, Func c) { return a(x) + c(x); });
        ok = ok && test_elementwise<uint8_t>("add_const", lanes, [](Var x, Func a, Func c) { return a(x) + (uint8_t)7; });
        ok = ok && test_elementwise<uint8_t>("add_big_const", lanes, [](Var x, Func a, Func c) { return a(x) + (uint8_t)200; });
        ok = ok && test_elementwise<uint8_t>("sub", lanes, [](Var x, Func a, Func c) { return a(x) - c(x); });
        ok = ok && test_elementwise<uint8_t>("sub_const", lanes, [](Var x, Func a, Func c) { return (uint8_t)3 - a(x); });
        // Bitwise operations and shifts.
        ok = ok && test_elementwise<uint8_t>("shl", lanes, [](Var x, Func a, Func c) { return a(x) << 3; });
        ok = ok && test_elementwise<uint8_t>("shr", lanes, [](Var x, Func a, Func c) { return a(x) >> 5; });
        ok = ok && test_elementwise<uint8_t>("xor", lanes, [](Var x, Func a, Func c) { return a(x) ^ c(x); });
        ok = ok && test_elementwise<uint8_t>("not", lanes, [](Var x, Func a, Func c) { return ~a(x); });
        ok = ok && test_elementwise<uint8_t>("mix", lanes, [](Var x, Func a, Func c) {
                 return ((a(x) << 2) | (c(x) >> 6)) ^ ~(a(x) & (uint8_t)0x5a);
             });
        // Multiplication, division and modulo by powers of two.
        ok = ok && test_elementwise<uint8_t>("mul_div_mod", lanes, [](Var x, Func a, Func c) {
                 return a(x) * 4 + c(x) / 8 + a(x) % 16;
             });
        // Signed lanes: arithmetic shifts and floor division.
        ok = ok && test_elementwise<int8_t>("signed", lanes, [](Var x, Func a, Func c) {
                 return (a(x) >> 2) + c(x) / 4 + a(x) % 8 - (c(x) << 1);
             });
        // Broadcasts and ramps of runtime values.
        ok = ok && test_elementwise<uint8_t>("ramp", lanes, [](Var x, Func a, Func c) {
                 return (a(x) ^ uniform_value) + cast<uint8_t>(x * 3);
             });
        // Selects on a condition that is uniform across the lanes.
        ok = ok && test_elementwise<uint8_t>("uniform_select", lanes, [](Var x, Func a, Func c) {
                 return select(uniform_cond > 0, a(x), c(x));
             });
        // Selects on a condition that varies across the lanes, which is
        // evaluated one lane at a time.
        ok = ok && test_elementwise<uint8_t>("varying_select", lanes, [](Var x, Func a, Func c) {
                 return select(a(x) < c(x) || x % 3 == 0, a(x), c(x) ^ (uint8_t)0x55);
             });
        // Uniform shifts by a runtime amount.
        ok = ok && test_elementwise<uint8_t>("uniform_shift", lanes, [](Var x, Func a, Func c) {
                 return (a(x) >> uniform_shift) ^ (c(x) << uniform_shift);
             });
        ok = ok && test_elementwise<int8_t>("uniform_signed_shift", lanes, [](Var x, Func a, Func c) {
                 return (a(x) >> uniform_signed_shift) + (c(x) << (-uniform_signed_shift));
             });
        // Tails.
        ok = ok && test_elementwise<uint8_t>("tail_shift_inwards", lanes, [](Var x, Func a, Func c) { return a(x) + c(x); }, 37);
        ok = ok && test_elementwise<uint8_t>("tail_guard_with_if", lanes, [](Var x, Func a, Func c) { return a(x) + c(x); }, 37, TailStrategy::GuardWithIf);
        ok = ok && test_elementwise<uint8_t>("tail_predicate_stores", lanes, [](Var x, Func a, Func c) { return a(x) + c(x); }, 37, TailStrategy::PredicateStores);
        ok = ok && test_elementwise<uint8_t>("tail_predicate_loads", lanes, [](Var x, Func a, Func c) { return a(x) + c(x); }, 37, TailStrategy::PredicateLoads);
        ok = ok && test_elementwise<uint8_t>("tail_round_up_and_blend", lanes, [](Var x, Func a, Func c) { return a(x) ^ c(x); }, 37, TailStrategy::RoundUpAndBlend);
    }

    for (int lanes : u16_lanes) {
        ok = ok && test_elementwise<uint16_t>("add_u16", lanes, [](Var x, Func a, Func c) { return a(x) + c(x); });
        ok = ok && test_elementwise<uint16_t>("sub_u16", lanes, [](Var x, Func a, Func c) { return a(x) - c(x); });
        ok = ok && test_elementwise<uint16_t>("bits_u16", lanes, [](Var x, Func a, Func c) {
                 return ((a(x) >> 3) & (uint16_t)0x0f0f) | (c(x) << 9) | ~(a(x) ^ c(x));
             });
        ok = ok && test_elementwise<int16_t>("signed_u16", lanes, [](Var x, Func a, Func c) {
                 return (a(x) >> 7) - c(x) / 16;
             });
    }

    if (!ok) {
        printf("Failed\n");
        return 1;
    }
    printf("Success!\n");
    return 0;
}
