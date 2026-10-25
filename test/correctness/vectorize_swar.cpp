#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <random>
#include <sstream>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Look for vector values in the producer of a Func vectorized with
// VectorizeIn::SWAR. The only vectors allowed are dense loads and stores of
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

// For a Func vectorized with VectorizeIn::SWAR inside a SIMD loop, look
// for vector arithmetic on the narrow lanes in its producer. Each SIMD
// lane should instead hold a word of packed narrow lanes, so the only
// narrow vectors allowed are loads (including lane-by-lane predicated
// loads), shuffles and reinterprets of them, comparisons that pick lanes
// for blends, and casts of per-word scalars.
class FindNarrowVectorOps : public IRVisitor {
    using IRVisitor::visit;

    const std::string &func;
    const int lane_bits, word_bits;
    int in_producer = 0;

    void check(const Expr &e) {
        const Type t = e.type();
        if (!in_producer || !t.is_vector()) {
            return;
        }
        if (t.bits() == lane_bits && !t.is_bool()) {
            if (narrow_ops.empty()) {
                std::cout << "Found narrow vector op in producer of " << func << ":\n  " << e << "\n";
            }
            narrow_ops.push_back(e);
        } else if (t.bits() == word_bits) {
            found_word_vectors = true;
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

#define CHECK(T)                       \
    void visit(const T *op) override { \
        check(op);                     \
        IRVisitor::visit(op);          \
    }
    CHECK(Add)
    CHECK(Sub)
    CHECK(Mul)
    CHECK(Div)
    CHECK(Mod)
    CHECK(Min)
    CHECK(Max)
    CHECK(Select)
#undef CHECK

    void visit(const Call *op) override {
        // Lanes of predicated loads are loaded one at a time, guarded by
        // if_then_else, before being packed into a word.
        if (!op->is_intrinsic(Call::if_then_else)) {
            check(op);
        }
        IRVisitor::visit(op);
    }

    // A narrowing cast of a per-word scalar (e.g. the base of a ramp, to be
    // splatted across the word) is legitimate, so casts only count as words.
    void visit(const Cast *op) override {
        if (in_producer && op->type.is_vector() && op->type.bits() == word_bits) {
            found_word_vectors = true;
        }
        IRVisitor::visit(op);
    }

public:
    std::vector<Expr> narrow_ops;
    bool found_producer = false, found_word_vectors = false;

    FindNarrowVectorOps(const std::string &func, int lane_bits, int word_bits)
        : func(func), lane_bits(lane_bits), word_bits(word_bits) {
    }
};

class FindNarrowVectorOpsPass : public IRMutator {
public:
    std::string func;
    int lane_bits, word_bits;
    bool ok = false;

    using IRMutator::mutate;
    Stmt mutate(const Stmt &s) override {
        FindNarrowVectorOps finder(func, lane_bits, word_bits);
        s.accept(&finder);
        ok = finder.found_producer && finder.narrow_ops.empty() && finder.found_word_vectors;
        if (!finder.found_producer) {
            printf("Did not find the producer of %s\n", func.c_str());
        } else if (!finder.found_word_vectors) {
            printf("Found no SIMD vectors of %d-bit words in the producer of %s\n", word_bits, func.c_str());
        }
        return s;
    }

    FindNarrowVectorOpsPass(const std::string &func, int lane_bits, int word_bits)
        : func(func), lane_bits(lane_bits), word_bits(word_bits) {
    }
};

// Compile the pipeline, checking that the producer of swar_func, vectorized
// with VectorizeIn::SWAR inside a SIMD loop, computes on SIMD vectors of
// words of packed lanes rather than on SIMD vectors of the lanes.
bool check_words_in_simd(Pipeline p, const Func &swar_func, int lane_bits, int word_bits) {
    FindNarrowVectorOpsPass checker(swar_func.name(), lane_bits, word_bits);
    p.add_custom_lowering_pass(&checker, nullptr);
    p.compile_jit();
    p.clear_custom_lowering_passes();
    return checker.ok;
}

class FindVectorsPass : public IRMutator {
public:
    std::vector<std::string> funcs;
    size_t num_swar_funcs;
    std::vector<bool> found_producer, has_vectors;

    using IRMutator::mutate;
    Stmt mutate(const Stmt &s) override {
        found_producer.clear();
        has_vectors.clear();
        for (size_t i = 0; i < funcs.size(); i++) {
            FindVectors finder(funcs[i], i < num_swar_funcs);
            s.accept(&finder);
            found_producer.push_back(finder.found_producer);
            has_vectors.push_back(!finder.vectors.empty());
        }
        return s;
    }

    FindVectorsPass(const std::vector<std::string> &funcs, size_t num_swar_funcs)
        : funcs(funcs), num_swar_funcs(num_swar_funcs) {
    }
};

// Compile the pipeline, checking that the producers of the swar_funcs have
// no vectors left in them, and that the producers of the simd_funcs do
// (i.e. that they were not lowered to general-purpose registers).
bool check_vectors(Pipeline p, const std::vector<Func> &swar_funcs,
                   const std::vector<Func> &simd_funcs = {}) {
    std::vector<std::string> names;
    for (const Func &f : swar_funcs) {
        names.push_back(f.name());
    }
    for (const Func &f : simd_funcs) {
        names.push_back(f.name());
    }
    FindVectorsPass checker(names, swar_funcs.size());
    p.add_custom_lowering_pass(&checker, nullptr);
    p.compile_jit();
    p.clear_custom_lowering_passes();
    bool ok = checker.found_producer.size() == names.size();
    for (size_t i = 0; ok && i < names.size(); i++) {
        const bool swar = i < swar_funcs.size();
        if (!checker.found_producer[i]) {
            printf("Did not find the producer of %s\n", names[i].c_str());
            ok = false;
        } else if (swar && checker.has_vectors[i]) {
            printf("SWAR-vectorized Func %s still contains vectors\n", names[i].c_str());
            ok = false;
        } else if (!swar && !checker.has_vectors[i]) {
            printf("SIMD-vectorized Func %s contains no vectors\n", names[i].c_str());
            ok = false;
        }
    }
    return ok;
}

bool check_no_vectors(Pipeline p, const Func &swar_func) {
    return check_vectors(std::move(p), {swar_func});
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
    f.bound(k, 0, 16).split(k, j, i, lanes).unroll(j).vectorize(i, lanes, VectorizeIn::SWAR);

    if (!check_no_vectors(Pipeline(f), f)) {
        return false;
    }
    Buffer<uint8_t> out = f.realize({16, nb});
    return check_equal("kscales", out, kscales_reference(in));
}

// A SWAR producer computed per block inside a SIMD consumer, which is how
// the scales get used in the q4_K dot product.
bool test_kscales_consumer(MemoryType storage) {
    const int nb = 23;
    Buffer<uint8_t> in = random_buffer<uint8_t>({12, nb});
    Buffer<int16_t> bs = random_buffer<int16_t>({8, nb});

    Func in_f("in_f"), f("kscales"), g("g");
    Var k("k"), b("b"), j("j"), i("i");
    in_f(i, b) = in(i, b);
    f(k, b) = kscales_decode(in_f, k, b);
    g(k, b) = cast<int32_t>(f(k + 8, b)) * cast<int32_t>(bs(k, b));

    g.bound(k, 0, 8).vectorize(k, 8);
    f.compute_at(g, b).bound(k, 0, 16).split(k, j, i, 4).unroll(j).vectorize(i, 4, VectorizeIn::SWAR);
    f.store_in(storage);

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

// Index bytes staged per block in MemoryType::GPR, by a SIMD- or a
// SWAR-vectorized loop, and read one at a time by a table lookup, which is
// how GGML's iq3_xxs reads its grid indices. Each 8-byte word of the stage
// is held in a general-purpose register, so the lookups extract the
// indices with shifts rather than from a SIMD register, or by reloading
// them from memory.
bool test_staged_indices(MemoryType storage, VectorizeIn where, int lanes, bool check_words = false) {
    const int nb = 21, per_block = 16;
    Buffer<uint8_t> in = random_buffer<uint8_t>({per_block, nb});
    Buffer<int32_t> table = random_buffer<int32_t>({256});

    Func idx("idx"), g("g");
    Var k("k"), b("b"), ko("ko"), ki("ki");
    idx(k, b) = in(k, b);
    g(k, b) = table(cast<int>(idx(k, b))) + k;

    g.bound(k, 0, per_block).unroll(k);
    idx.compute_at(g, b).store_in(storage).split(k, ko, ki, lanes).unroll(ko).vectorize(ki, lanes, where);

    Buffer<int32_t> out = g.realize({per_block, nb});
    Buffer<int32_t> ref(per_block, nb);
    for (int bb = 0; bb < nb; bb++) {
        for (int kk = 0; kk < per_block; kk++) {
            ref(kk, bb) = table(in(kk, bb)) + kk;
        }
    }
    if (!check_equal("staged_indices", out, ref)) {
        return false;
    }

    if (check_words) {
        // The stage's words never reach a SIMD register: the vector type of
        // a word is gone from the optimized LLVM IR. (Checked against the
        // same schedule in MemoryType::Stack, which LLVM keeps as vectors,
        // so that a change in LLVM doesn't silently make this vacuous.)
        auto llvm_ir = [&](MemoryType m) {
            idx.store_in(m);
            const std::string file = Internal::get_test_tmp_dir() + "vectorize_swar_staged.ll";
            g.compile_to_llvm_assembly(file, {}, "staged", get_jit_target_from_environment().with_feature(Target::NoRuntime));
            std::ifstream f(file);
            std::stringstream s;
            s << f.rdbuf();
            return s.str();
        };
        const std::string word_vector = "<" + std::to_string(lanes) + " x i8>";
        const std::string gpr_ir = llvm_ir(MemoryType::GPR), stack_ir = llvm_ir(MemoryType::Stack);
        idx.store_in(storage);
        if (stack_ir.find(word_vector) == std::string::npos) {
            printf("staged_indices: LLVM no longer keeps the stack stage in %s vectors; skipping the check\n",
                   word_vector.c_str());
        } else if (gpr_ir.find(word_vector) != std::string::npos) {
            printf("staged_indices: the stage in MemoryType::GPR still uses %s vectors\n", word_vector.c_str());
            return false;
        }
    }
    return true;
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
    wrapper.compute_at(acc, r).split(k, j, i, 4).unroll(j).vectorize(i, 4, VectorizeIn::SWAR);

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

// Update stages can be vectorized with VectorizeIn::SWAR too, while a
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
    f.compute_at(out, b).split(k, j, i, 4).unroll(j).vectorize(i, 4, VectorizeIn::SWAR);
    f.update(0).vectorize(k, 4, VectorizeIn::SWAR);
    f.update(1).vectorize(r, 4, VectorizeIn::SWAR);
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

// The decode of several blocks at once, one block per SIMD lane, each
// lane a word of four packed bytes ("SWAR within a vector register").
bool test_kscales_simd_blocks(int simd_lanes) {
    const int nb = 37;
    Buffer<uint8_t> in = random_buffer<uint8_t>({12, nb});

    Func in_f("in_f"), f("kscales");
    Var k("k"), b("b"), j("j"), i("i");
    in_f(i, b) = in(i, b);
    f(k, b) = kscales_decode(in_f, k, b);
    f.bound(k, 0, 16).split(k, j, i, 4).unroll(j).vectorize(i, 4, VectorizeIn::SWAR).vectorize(b, simd_lanes);

    if (!check_words_in_simd(Pipeline(f), f, 8, 32)) {
        printf("  in test kscales_simd_blocks with %d SIMD lanes\n", simd_lanes);
        return false;
    }
    Buffer<uint8_t> out = f.realize({16, nb});
    return check_equal("kscales_simd_blocks", out, kscales_reference(in));
}

// A SWAR producer computed inside a SIMD-vectorized loop of its consumer.
bool test_kscales_in_simd_consumer() {
    const int nb = 23;
    Buffer<uint8_t> in = random_buffer<uint8_t>({12, nb});
    Buffer<int16_t> bs = random_buffer<int16_t>({8, nb});

    Func in_f("in_f"), f("kscales"), g("g");
    Var k("k"), b("b"), bo("bo"), bi("bi"), j("j"), i("i");
    in_f(i, b) = in(i, b);
    f(k, b) = kscales_decode(in_f, k, b);
    g(k, b) = cast<int32_t>(f(k + 8, b)) * cast<int32_t>(bs(k, b));

    g.bound(k, 0, 8).split(b, bo, bi, 4).reorder(bi, k, bo).vectorize(bi);
    f.compute_at(g, bi).bound(k, 0, 16).split(k, j, i, 4).unroll(j).vectorize(i, 4, VectorizeIn::SWAR);

    if (!check_words_in_simd(Pipeline(g), f, 8, 32)) {
        printf("  in test kscales_in_simd_consumer\n");
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
    return check_equal("kscales_in_simd_consumer", out, ref);
}

// The number of SIMD lanes of words test_elementwise vectorizes across, in
// a SIMD loop around the SWAR loop, or 1 for no SIMD loop.
int simd_lanes = 1;

// Check a SWAR-vectorized Func against the same Func computed without
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

    Func f(std::string(name) + "_swar"), ref(std::string(name) + "_ref");
    f(x) = def(x, a, c);
    ref(x) = def(x, a, c);
    if (simd_lanes > 1) {
        Var xo("xo"), xw("xw"), y("y"), xi("xi");
        f.split(x, xw, xi, lanes, tail)
            .split(xw, xo, y, simd_lanes, TailStrategy::GuardWithIf)
            .vectorize(xi, VectorizeIn::SWAR)
            .vectorize(y);
        if (!check_words_in_simd(Pipeline(f), f, sizeof(T) * 8, sizeof(T) * 8 * lanes)) {
            printf("  in test %s with %d lanes in %d SIMD lanes\n", name, lanes, simd_lanes);
            return false;
        }
    } else {
        f.vectorize(x, lanes, VectorizeIn::SWAR, tail);
        if (!check_no_vectors(Pipeline(f), f)) {
            printf("  in test %s with %d lanes\n", name, lanes);
            return false;
        }
    }
    Buffer<T> out = f.realize({size});
    Buffer<T> expected = ref.realize({size});
    if (!check_equal(name, out, expected)) {
        printf("  in test %s with %d lanes in %d SIMD lanes\n", name, lanes, simd_lanes);
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
    ok = ok && test_kscales_consumer(MemoryType::Auto);
    ok = ok && test_kscales_consumer(MemoryType::Register);
    ok = ok && test_kscales_consumer(MemoryType::GPR);
    ok = ok && test_kscales_wrapper();
    ok = ok && test_update_and_sibling();
    // SIMD loops around SWAR loops.
    ok = ok && test_kscales_simd_blocks(4);
    ok = ok && test_kscales_simd_blocks(8);
    ok = ok && test_kscales_in_simd_consumer();
    // Bytes staged in MemoryType::GPR.
    for (int lanes : {4, word_bits / 8}) {
        for (MemoryType m : {MemoryType::Stack, MemoryType::GPR}) {
            ok = ok && test_staged_indices(m, VectorizeIn::SIMD, lanes);
            ok = ok && test_staged_indices(m, VectorizeIn::SWAR, lanes);
        }
    }
    ok = ok && test_staged_indices(MemoryType::GPR, VectorizeIn::SIMD, word_bits / 8, true);

    std::vector<int> u8_lanes = {2, 4};
    std::vector<int> u16_lanes = {2};
    if (word_bits >= 64) {
        u8_lanes.push_back(8);
        u16_lanes.push_back(4);
    }

    // Each SWAR loop on its own, then inside a SIMD loop.
    for (int simd : {1, 4}) {
        simd_lanes = simd;
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
    }

    if (!ok) {
        printf("Failed\n");
        return 1;
    }
    printf("Success!\n");
    return 0;
}
