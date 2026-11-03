#include "Halide.h"
#include "check_call_graphs.h"
#include "test_sharding.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>

namespace {

using std::map;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

// hoist_invariants() lifts a loop-invariant factor out of a sum reduction:
//   sum_k(scale(i,j) * inner(i,j,k)) = scale(i,j) * sum_k(inner(i,j,k))
// It does not change any types: the returned intermediate accumulates the
// factor-free body at its natural type. (Retyping the accumulation for a
// dot-product-friendly integer type is the job of change_type().)
int hoist_invariants_test() {
    ImageParam A{Int(8), 2, "A"};
    ImageParam B{Int(8), 2, "B"};
    ImageParam As{Float(16), 1, "As"};
    ImageParam Bs{Float(16), 1, "Bs"};

    Var i{"i"}, j{"j"};
    RDom k({{0, A.dim(1).extent() / 4 * 4}}, "k");
    RVar ko{"ko"}, ki{"ki"};

    Func C{"C"};
    C(i, j) += widening_mul(As(i), Bs(j)) * cast(Int(32), widening_mul(A(i, k), B(j, k)));
    C.bound(i, 0, A.dim(0).extent());
    C.bound(j, 0, B.dim(0).extent());
    C.update().split(k, ko, ki, 4);

    // widening_mul(As(i), Bs(j)) is invariant in k, so hoisting moves it out of
    // the reduction: the intermediate accumulates only the inner product and the
    // scale is applied once during write-back. The body's type (Float(32)) is
    // unchanged.
    Func C_intm = C.update().hoist_invariants();

    internal_assert(C_intm.types()[0] == Float(32))
        << "hoist_invariants: expected C_intm to keep its natural Float(32) type, got "
        << C_intm.types()[0] << "\n";

    // Numerical correctness: result must match a reference that applies the full
    // non-hoisted reduction.
    const int M = 8, N = 8, K = 16;
    Buffer<int8_t> a_buf(M, K), b_buf(N, K);
    Buffer<float16_t> as_buf(M), bs_buf(N);
    for (int m = 0; m < M; m++) {
        for (int n_k = 0; n_k < K; n_k++) {
            a_buf(m, n_k) = (int8_t)((m + n_k) % 7 - 3);
        }
        as_buf(m) = float16_t((float)(m + 1) * 0.5f);
    }
    for (int n = 0; n < N; n++) {
        for (int n_k = 0; n_k < K; n_k++) {
            b_buf(n, n_k) = (int8_t)((n + n_k + 1) % 5 - 2);
        }
        bs_buf(n) = float16_t((float)(n + 1) * 0.25f);
    }
    A.set(a_buf);
    B.set(b_buf);
    As.set(as_buf);
    Bs.set(bs_buf);

    Buffer<float> result = C.realize({M, N});

    // Reference: plain reduction without hoisting.
    Buffer<float> ref(M, N);
    ref.fill(0.f);
    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            for (int kk = 0; kk < K; kk++) {
                ref(m, n) += (float)as_buf(m) * (float)bs_buf(n) *
                             (float)((int32_t)(int16_t)((int16_t)a_buf(m, kk) * (int16_t)b_buf(n, kk)));
            }
        }
    }

    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            internal_assert(std::abs(result(m, n) - ref(m, n)) < 1e-6f)
                << "hoist_invariants mismatch at (" << m << ", " << n << "): "
                << result(m, n) << " vs ref " << ref(m, n) << "\n";
        }
    }

    return 0;
}

// An invariant factor may be nested inside its own sub-product rather than
// sitting as one of a Mul's two immediate operands:
//   (scaleA(i) * castA) * (scaleB(i) * castB)
// -- the way two independently-scaled operands naturally compose. Finding both
// scales requires flattening the whole multiplicative chain into leaves and
// partitioning each one by invariance, not just checking a binary node's two
// immediate children.
int hoist_invariants_scattered_factors_test() {
    const int K = 64;
    ImageParam A{Int(8), 1, "A"};
    ImageParam B{Int(8), 1, "B"};
    ImageParam ScaleA{Float(32), 1, "ScaleA"};
    ImageParam ScaleB{Float(32), 1, "ScaleB"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += (cast<float>(A(r)) * ScaleA(i)) * (cast<float>(B(r)) * ScaleB(i));

    Func Acc_intm = Acc.update().hoist_invariants();
    internal_assert(Acc_intm.types()[0] == Float(32))
        << "hoist_invariants: expected the intermediate to keep Float(32), got "
        << Acc_intm.types()[0] << "\n";
    Acc_intm.compute_root();

    Buffer<int8_t> a_buf(K), b_buf(K);
    Buffer<float> scale_a_buf(1), scale_b_buf(1);
    for (int k = 0; k < K; k++) {
        a_buf(k) = 127;
        b_buf(k) = 127;
    }
    scale_a_buf(0) = 2.0f;
    scale_b_buf(0) = 3.0f;
    A.set(a_buf);
    B.set(b_buf);
    ScaleA.set(scale_a_buf);
    ScaleB.set(scale_b_buf);

    Buffer<float> result = Acc.realize({1});
    const float expected = 2.0f * 3.0f * (float)K * 127.0f * 127.0f;
    internal_assert(result(0) == expected)
        << "hoist_invariants scattered factors: got " << result(0) << ", expected " << expected << "\n";

    return 0;
}

// Same shape as hoist_invariants_scattered_factors_test, but with UInt(8)
// operands, to exercise the unsigned path.
int hoist_invariants_scattered_factors_unsigned_test() {
    const int K = 64;
    ImageParam A{UInt(8), 1, "A"};
    ImageParam B{UInt(8), 1, "B"};
    ImageParam ScaleA{Float(32), 1, "ScaleA"};
    ImageParam ScaleB{Float(32), 1, "ScaleB"};

    Var i{"i"};
    RDom r(0, K, "r");

    Func Acc{"Acc"};
    Acc(i) = 0.0f;
    Acc(i) += (cast<float>(A(r)) * ScaleA(i)) * (cast<float>(B(r)) * ScaleB(i));

    Func Acc_intm = Acc.update().hoist_invariants();
    internal_assert(Acc_intm.types()[0] == Float(32))
        << "hoist_invariants: expected the intermediate to keep Float(32), got "
        << Acc_intm.types()[0] << "\n";
    Acc_intm.compute_root();

    Buffer<uint8_t> a_buf(K), b_buf(K);
    Buffer<float> scale_a_buf(1), scale_b_buf(1);
    for (int k = 0; k < K; k++) {
        a_buf(k) = 255;
        b_buf(k) = 255;
    }
    scale_a_buf(0) = 2.0f;
    scale_b_buf(0) = 3.0f;
    A.set(a_buf);
    B.set(b_buf);
    ScaleA.set(scale_a_buf);
    ScaleB.set(scale_b_buf);

    Buffer<float> result = Acc.realize({1});
    const float expected = 2.0f * 3.0f * (float)K * 255.0f * 255.0f;
    internal_assert(result(0) == expected)
        << "hoist_invariants scattered factors (unsigned): got " << result(0) << ", expected " << expected << "\n";

    return 0;
}

// A sum reduction can contain independently scaled terms. Each outer addend
// gets its own scalar intermediate, and the write-back applies the scale while
// merging those intermediates into the original accumulator.
int hoist_invariants_multiple_terms_test() {
    const int K = 16;
    RDom r(0, K, "r");

    Func f{"multiple_terms"};
    f() = 0;
    f() += 2 * (r + 1) + 3 * (2 * r + 1);

    FuncVec intms = f.update().hoist_invariants();
    internal_assert(intms.size() == 2)
        << "hoist_invariants multiple terms: expected two intermediates, got "
        << intms.size() << "\n";
    internal_assert(intms[0].name() == f.name() + "_intm0" &&
                    intms[1].name() == f.name() + "_intm1")
        << "hoist_invariants multiple terms: unexpected intermediate names\n";
    for (Func &intm : intms) {
        intm.compute_root();
    }

    Buffer<int> result = f.realize();
    int expected = 0;
    for (int k = 0; k < K; ++k) {
        expected += 2 * (k + 1) + 3 * (2 * k + 1);
    }
    internal_assert(result() == expected)
        << "hoist_invariants multiple terms: got " << result()
        << ", expected " << expected << "\n";
    return 0;
}

// Splitting outer terms is useful even when none has an invariant factor.
// Each term is reduced independently and merged at the original accumulator.
int hoist_invariants_unscaled_terms_test() {
    const int K = 16;
    Var x{"x"};
    RDom r(0, K, "r");

    Func g{"unscaled_g"}, h{"unscaled_h"}, f{"unscaled_terms"};
    g(x) = x + 1;
    h(x) = x * x + 3;
    f() = 0;
    f() += g(r) + h(r);

    FuncVec intms = f.update().hoist_invariants();
    internal_assert(intms.size() == 2)
        << "hoist_invariants unscaled terms: expected two intermediates, got "
        << intms.size() << "\n";
    for (Func &intm : intms) {
        intm.compute_root();
    }

    Buffer<int> result = f.realize();
    int expected = 0;
    for (int k = 0; k < K; ++k) {
        expected += (k + 1) + (k * k + 3);
    }
    internal_assert(result() == expected)
        << "hoist_invariants unscaled terms: got " << result()
        << ", expected " << expected << "\n";
    return 0;
}

// Tuple outputs are flattened independently. The FuncVec is ordered by tuple
// output index, then by the order of that output's flattened outer terms.
int hoist_invariants_tuple_terms_test() {
    const int K = 8;
    RDom r(0, K, "r");

    Func f{"tuple_terms"};
    f() = Tuple(0, 0);
    f() = Tuple(f()[0] + 2 * (r + 1) + 3 * (r + 2),
                f()[1] + 4 * (r + 3));

    FuncVec intms = f.update().hoist_invariants();
    internal_assert(intms.size() == 3)
        << "hoist_invariants tuple terms: expected three intermediates, got "
        << intms.size() << "\n";
    for (size_t i = 0; i < intms.size(); ++i) {
        internal_assert(intms[i].name() == f.name() + "_intm" + std::to_string(i))
            << "hoist_invariants tuple terms: unexpected intermediate ordering\n";
        intms[i].compute_root();
    }

    Realization result = f.realize();
    int expected0 = 0, expected1 = 0;
    for (int k = 0; k < K; ++k) {
        expected0 += 2 * (k + 1) + 3 * (k + 2);
        expected1 += 4 * (k + 3);
    }
    internal_assert(result[0].as<int>()() == expected0 &&
                    result[1].as<int>()() == expected1)
        << "hoist_invariants tuple terms: incorrect result\n";
    return 0;
}

// hoist_invariants() with outer Min and additive factor:
//   min_k(offset(i) + body(i, k)) = offset(i) + min_k(body(i, k))
// The intermediate accumulates min without the offset; write-back adds it once.
int hoist_invariants_min_test() {
    ImageParam offset_p{Float(32), 1, "offset_p"};
    ImageParam data_p{Float(32), 2, "data_p"};

    Var i{"i"};
    const int K = 16;
    RDom k(0, K, "k");

    Func C{"C"};
    C(i) = Float(32).max();
    C(i) = min(C(i), offset_p(i) + data_p(i, k));

    Func C_intm = C.update().hoist_invariants();
    C_intm.compute_root();

    const int M = 8;
    Buffer<float> off(M), dat(M, K);
    for (int m = 0; m < M; m++) {
        off(m) = (float)(m + 1);
        for (int kk = 0; kk < K; kk++) {
            dat(m, kk) = (float)(((m * K + kk) % 7) - 3);
        }
    }
    offset_p.set(off);
    data_p.set(dat);

    Buffer<float> result = C.realize({M});

    Buffer<float> ref(M);
    for (int m = 0; m < M; m++) {
        float v = std::numeric_limits<float>::max();
        for (int kk = 0; kk < K; kk++) {
            v = std::min(v, off(m) + dat(m, kk));
        }
        ref(m) = v;
    }

    for (int m = 0; m < M; m++) {
        internal_assert(result(m) == ref(m))
            << "hoist_invariants min mismatch at " << m << ": "
            << result(m) << " vs ref " << ref(m) << "\n";
    }
    return 0;
}

// hoist_invariants() with outer Or (bool) and And factor:
//   or_k(mask(i) && check(i, k)) = mask(i) && or_k(check(i, k))
// The intermediate accumulates or without the mask; write-back applies it once.
int hoist_invariants_or_test() {
    ImageParam mask_p{Bool(), 1, "mask_p"};
    ImageParam check_p{Bool(), 2, "check_p"};

    Var i{"i"};
    const int K = 16;
    RDom k(0, K, "k");

    Func valid{"valid"};
    valid(i) = cast<bool>(false);
    valid(i) = valid(i) || (mask_p(i) && check_p(i, k));

    Func valid_intm = valid.update().hoist_invariants();
    valid_intm.compute_root();

    const int M = 8;
    Buffer<bool> mask(M), chk(M, K);
    for (int m = 0; m < M; m++) {
        mask(m) = (m % 2 == 0);
        for (int kk = 0; kk < K; kk++) {
            chk(m, kk) = ((m + kk) % 3 == 0);
        }
    }
    mask_p.set(mask);
    check_p.set(chk);

    Buffer<bool> result = valid.realize({M});

    for (int m = 0; m < M; m++) {
        bool ref = false;
        for (int kk = 0; kk < K; kk++) {
            ref = ref || (mask(m) && chk(m, kk));
        }
        internal_assert(result(m) == ref)
            << "hoist_invariants or mismatch at " << m << ": "
            << (int)result(m) << " vs ref " << (int)ref << "\n";
    }
    return 0;
}

// hoist_invariants() must not disturb strict_float: the reduction below rounds
// each term to float before summing, and both the reference and the hoisted
// intermediate must observe that same rounding (giving exactly 0).
int hoist_invariants_strict_float_test() {
    Buffer<int32_t> data(2);
    data(0) = 16777217;
    data(1) = -16777216;

    RDom k(0, 2, "k");

    Func f{"f"};
    f() = 0.0f;
    f() += 1.5f * strict_float(cast<float>(data(k)));

    Func intm = f.update().hoist_invariants();
    intm.compute_root();

    internal_assert(intm.types()[0] == Float(32))
        << "hoist_invariants strict_float: expected intm to remain Float(32), got "
        << intm.types()[0] << "\n";

    Buffer<float> result = f.realize();
    internal_assert(result() == 0.0f)
        << "hoist_invariants strict_float mismatch: " << result()
        << " vs ref 0\n";

    return 0;
}

// A loop-invariant RDom predicate must be preserved on the write-back update.
// When enabled is false, the original reduction executes no iterations, so the
// hoisted factor (and its failing require) must not be evaluated.
int hoist_invariants_predicated_rdom_test() {
    Param<bool> enabled{"enabled"};
    RDom r(0, 4, "r");
    r.where(enabled);

    Func f{"f"};
    f() = 0;
    f() += require(enabled, 2, "hoisted factor evaluated outside RDom predicate") * (r + 1);

    Func intm = f.update().hoist_invariants();
    intm.compute_root();

    Module module = f.compile_to_module(f.infer_arguments(), "hoist_invariants_predicated_rdom");
    bool inside_predicate = false;
    bool found_writeback = false;
    for (const LoweredFunc &lowered_func : module.functions()) {
        visit_with(
            lowered_func.body,
            [&](auto *self, const IfThenElse *op) {
                const bool old_inside_predicate = inside_predicate;
                inside_predicate |= expr_uses_var(op->condition, enabled.name());
                (*self)(op->then_case);
                inside_predicate = old_inside_predicate;

                if (op->else_case.defined()) {
                    (*self)(op->else_case);
                }
            },
            [&](auto *self, const Store *op) {
                const bool predicated =
                    inside_predicate || expr_uses_var(op->predicate, enabled.name());
                if (op->name == f.name() && predicated) {
                    visit_with(op->value, [&](auto *, const Load *load) {
                        found_writeback |= load->name == intm.name();
                    });
                }
                self->visit_base(op);
            });
    }
    internal_assert(found_writeback)
        << "hoist_invariants predicated RDom: lowered pipeline had no write-back "
        << "guarded by " << enabled.name() << " that reads " << intm.name() << "\n";

    enabled.set(false);
    Buffer<int> disabled_result = f.realize();
    internal_assert(disabled_result() == 0)
        << "hoist_invariants predicated RDom: disabled result was "
        << disabled_result() << ", expected 0\n";

    enabled.set(true);
    Buffer<int> enabled_result = f.realize();
    internal_assert(enabled_result() == 20)
        << "hoist_invariants predicated RDom: enabled result was "
        << enabled_result() << ", expected 20\n";

    return 0;
}

// A term that doesn't use the RVars still runs once per point of the predicated
// domain, so its intermediate must keep the RDom predicate. Here k(x) is added
// for the 6 of 9 values of r that pass, not all 9.
int hoist_invariants_predicated_rvar_free_term_test() {
    const int W = 5, K = 9;
    Var x{"x"};
    RDom r(0, K, "r");
    r.where(r % 3 != 0);
    Func a{"a"}, s{"s"}, k{"k"};
    a(x, _) = x * 7 - _0 * 3 + 1;
    s(x) = x - 2;
    k(x) = 3 * x + 4;

    Func f{"f"};
    f(x) = 0;
    f(x) += a(x, r) * s(x) + k(x);
    f.update().hoist_invariants();

    Buffer<int> out = f.realize({W});
    for (int i = 0; i < W; i++) {
        int ref = 0;
        for (int j = 0; j < K; j++) {
            if (j % 3 != 0) {
                ref += (i * 7 - j * 3 + 1) * (i - 2) + 3 * i + 4;
            }
        }
        internal_assert(out(i) == ref)
            << "hoist_invariants predicated RVar-free term: result at " << i
            << " was " << out(i) << ", expected " << ref << "\n";
    }
    return 0;
}

// hoist_invariants() composes with rfactor(): rfactor first preserves r.y as a
// pure Var u, so scale(u) becomes invariant over the intermediate's remaining
// reduction over r.x and can then be hoisted from that intermediate.
int hoist_invariants_after_rfactor_test() {
    ImageParam scale_p{Float(32), 1, "scale_p"};
    ImageParam data_p{Int(32), 2, "data_p"};

    Var u{"u"};
    const int X = 8, Y = 4;
    RDom r(0, X, 0, Y, "r");

    Func f{"f"};
    f() = 0.0f;
    f() += scale_p(r.y) * data_p(r.x, r.y);

    // Preserve r.y as u; the intermediate now reduces only over r.x, and
    // scale_p(u) is invariant across that reduction.
    Func intm = f.update().rfactor({{r.y, u}});
    Func intm2 = intm.update().hoist_invariants();
    intm.compute_root();
    intm2.compute_root();

    Buffer<float> scale(Y);
    Buffer<int> data(X, Y);
    for (int y = 0; y < Y; y++) {
        scale(y) = (float)(y + 1);
        for (int x = 0; x < X; x++) {
            data(x, y) = (x + 2 * y) % 7 - 3;
        }
    }
    scale_p.set(scale);
    data_p.set(data);

    Buffer<float> result = f.realize();

    float ref = 0.0f;
    for (int y = 0; y < Y; y++) {
        int partial = 0;
        for (int x = 0; x < X; x++) {
            partial += data(x, y);
        }
        ref += scale(y) * (float)partial;
    }

    internal_assert(result() == ref)
        << "hoist_invariants after rfactor mismatch: "
        << result() << " vs ref " << ref << "\n";

    return 0;
}

bool calls_func(const Expr &e, const string &name) {
    bool found = false;
    visit_with(e, [&](auto *self, const Call *op) {
        found |= op->call_type == Call::Halide && op->name == name;
        self->visit_base(op);
    });
    return found;
}

// A blocked reduction with one factor per block, e.g. a block-scaled dot product
//   out = sum_k(scale(k / 32) * data(k)).
// After split(k, ko, ki, 32) + rfactor(ko, u), the intermediate's index is
// u*32 + ki, so the factor reads scale((u*32 + ki) / 32). That is not
// syntactically free of ki, but it equals scale(u) for every ki in [0, 32).
// hoist_invariants() must decide invariance modulo the RVar bounds, both when
// the split divides the extent and when it leaves a guarded tail.
int hoist_invariants_block_index_test(bool constant_extent, int n) {
    constexpr int block = 32;
    ImageParam data_p{Int(8), 1, "data_p"};
    ImageParam scale_p{Float(32), 1, "scale_p"};

    Var u{"u"};
    RDom r(0, constant_extent ? Expr(n) : data_p.dim(0).extent(), "r");
    RVar ko{"ko"}, ki{"ki"};

    Func out{"out"};
    out() = 0.0f;
    out() += cast<float>(data_p(r)) * scale_p(r / block);
    out.update().split(r, ko, ki, block);
    Func blocks = out.update().rfactor(ko, u);
    Func blocks_intm = blocks.update().hoist_invariants();
    blocks.compute_root();
    blocks_intm.compute_root();

    internal_assert(!calls_func(blocks_intm.update_value(), Func(scale_p).name()))
        << "hoist_invariants block index: the per-block factor was not hoisted out of "
        << blocks_intm.name() << ": " << blocks_intm.update_value() << "\n";
    internal_assert(calls_func(blocks.update_value(), Func(scale_p).name()))
        << "hoist_invariants block index: the write-back of " << blocks.name()
        << " does not apply the per-block factor: " << blocks.update_value() << "\n";

    const int num_blocks = (n + block - 1) / block;
    Buffer<int8_t> data(n);
    Buffer<float> scale(num_blocks);
    for (int k = 0; k < n; k++) {
        data(k) = (int8_t)((k * 7) % 11 - 5);
    }
    for (int b = 0; b < num_blocks; b++) {
        scale(b) = (float)(b + 1) * 0.5f;
    }
    data_p.set(data);
    scale_p.set(scale);

    Buffer<float> result = out.realize();

    float ref = 0.0f;
    for (int k = 0; k < n; k++) {
        ref += (float)data(k) * scale(k / block);
    }
    internal_assert(result() == ref)
        << "hoist_invariants block index (n = " << n << "): "
        << result() << " vs ref " << ref << "\n";

    return 0;
}

// The same reduction split twice, into blocks of 32 and then lanes of 4, with
// rfactor preserving both the block (u) and the lane within it (lane), so each
// lane keeps its own partial sum. The factor reads
// scale((u*32 + lane*4 + ki) / 32), which only simplifies to scale(lane/8 + u)
// given 0 <= ki < 4; that is invariant over ki and must be hoisted. The lane is
// a pure Var with no recorded range, so the factor keeps a lane/8 term, which
// is zero for the lanes 0..7 that are computed.
int hoist_invariants_block_lane_test(int n) {
    constexpr int block = 32, lane_width = 4;
    ImageParam data_p{Int(8), 1, "data_p"};
    ImageParam scale_p{Float(32), 1, "scale_p"};

    Var u{"u"}, lane{"lane"};
    RDom r(0, data_p.dim(0).extent(), "r");
    RVar ko{"ko"}, k{"k"}, kl{"kl"}, ki{"ki"};

    Func out{"out"};
    out() = 0.0f;
    out() += cast<float>(data_p(r)) * scale_p(r / block);
    out.update().split(r, ko, k, block).split(k, kl, ki, lane_width);
    Func blocks = out.update().rfactor({{kl, lane}, {ko, u}});
    Func blocks_intm = blocks.update().hoist_invariants();
    blocks.compute_root().vectorize(lane);
    blocks.update().vectorize(lane);
    blocks_intm.compute_at(blocks, u).vectorize(lane);

    internal_assert(!calls_func(blocks_intm.update_value(), Func(scale_p).name()))
        << "hoist_invariants block lane: the per-block factor was not hoisted out of "
        << blocks_intm.name() << ": " << blocks_intm.update_value() << "\n";
    internal_assert(calls_func(blocks.update_value(), Func(scale_p).name()))
        << "hoist_invariants block lane: the write-back of " << blocks.name()
        << " does not apply the per-block factor: " << blocks.update_value() << "\n";

    const int num_blocks = (n + block - 1) / block;
    Buffer<int8_t> data(n);
    Buffer<float> scale(num_blocks);
    for (int i = 0; i < n; i++) {
        data(i) = (int8_t)((i * 7) % 11 - 5);
    }
    for (int b = 0; b < num_blocks; b++) {
        scale(b) = (float)(b + 1) * 0.5f;
    }
    data_p.set(data);
    scale_p.set(scale);

    Buffer<float> result = out.realize();

    float ref = 0.0f;
    for (int i = 0; i < n; i++) {
        ref += (float)data(i) * scale(i / block);
    }
    internal_assert(result() == ref)
        << "hoist_invariants block lane (n = " << n << "): "
        << result() << " vs ref " << ref << "\n";

    return 0;
}

// The same block-scaled reduction, with each block of 32 split into two chunks
// of 16 and each chunk into lanes of 4, and rfactor preserving the block (u)
// and the lane (lane), so each lane keeps a partial sum over both chunks. The
// factor reads scale((u*32 + kc*16 + lane*4 + ki) / 32), which only reduces to
// scale((u*8 + kc*4 + lane) / 8) given the RVar bounds; dropping kc needs
// lane in [0, 4). lane is a pure Var of the intermediate, so the schedule
// supplies that range with bound().
Func block_two_chunk_reduction(const ImageParam &data_p, const ImageParam &scale_p,
                               const Var &u, const Var &lane, Func &out) {
    RDom r(0, data_p.dim(0).extent(), "r");
    RVar ko{"ko"}, k{"k"}, kc{"kc"}, kv{"kv"}, kl{"kl"}, ki{"ki"};
    out() = 0.0f;
    out() += cast<float>(data_p(r)) * scale_p(r / 32);
    out.update().split(r, ko, k, 32).split(k, kc, kv, 16).split(kv, kl, ki, 4);
    return out.update().rfactor({{kl, lane}, {ko, u}});
}

int hoist_invariants_block_two_chunk_test(int n) {
    constexpr int block = 32;
    ImageParam data_p{Int(8), 1, "data_p"};
    ImageParam scale_p{Float(32), 1, "scale_p"};
    Var u{"u"}, lane{"lane"};
    Func out{"out"};
    Func blocks = block_two_chunk_reduction(data_p, scale_p, u, lane, out);
    blocks.bound(lane, 0, 4);
    Func blocks_intm = blocks.update().hoist_invariants();
    blocks.compute_root().vectorize(lane);
    blocks.update().vectorize(lane);
    blocks_intm.compute_at(blocks, u).vectorize(lane);

    internal_assert(!calls_func(blocks_intm.update_value(), Func(scale_p).name()))
        << "hoist_invariants two chunks: the per-block factor was not hoisted out of "
        << blocks_intm.name() << ": " << blocks_intm.update_value() << "\n";
    internal_assert(calls_func(blocks.update_value(), Func(scale_p).name()))
        << "hoist_invariants two chunks: the write-back of " << blocks.name()
        << " does not apply the per-block factor: " << blocks.update_value() << "\n";

    const int num_blocks = (n + block - 1) / block;
    Buffer<int8_t> data(n);
    Buffer<float> scale(num_blocks);
    for (int i = 0; i < n; i++) {
        data(i) = (int8_t)((i * 7) % 11 - 5);
    }
    for (int b = 0; b < num_blocks; b++) {
        scale(b) = (float)(b + 1) * 0.5f;
    }
    data_p.set(data);
    scale_p.set(scale);

    Buffer<float> result = out.realize();

    float ref = 0.0f;
    for (int i = 0; i < n; i++) {
        ref += (float)data(i) * scale(i / block);
    }
    internal_assert(result() == ref)
        << "hoist_invariants two chunks (n = " << n << "): "
        << result() << " vs ref " << ref << "\n";

    return 0;
}

// The hoisted factor scale(u) equals the original only for lane in [0, 4).
// A consumer reading the intermediate outside that range must get an error,
// both while the bound stands (Halide checks explicit bounds) and if the bound
// is widened after hoisting (the hoisted factor checks the range it assumed;
// since bound() fixes the region computed, any realization then fails).
int hoist_invariants_block_two_chunk_out_of_bounds_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    constexpr int n = 64, num_blocks = n / 32;
    for (bool widen : {false, true}) {
        ImageParam data_p{Int(8), 1, "data_p"};
        ImageParam scale_p{Float(32), 1, "scale_p"};
        Var u{"u"}, lane{"lane"};
        Func out{"out"};
        Func blocks = block_two_chunk_reduction(data_p, scale_p, u, lane, out);
        blocks.bound(lane, 0, 4);
        Func blocks_intm = blocks.update().hoist_invariants();
        blocks_intm.compute_root();
        if (widen) {
            blocks.bound(lane, 0, 8);
        }

        Buffer<int8_t> data(n);
        Buffer<float> scale(num_blocks);
        data.fill(1);
        scale.fill(1.0f);
        data_p.set(data);
        scale_p.set(scale);

        const string expected = widen ? "hoist_invariants() hoisted a factor" : "do not cover required region";
        // A consumer that reads all 8 lanes. (Realizing blocks itself over 8
        // lanes would just compute the bounded ones.)
        Func reader{"reader"};
        reader(lane, u) = blocks(lane, u);
        blocks.compute_root();

        bool error = false;
        try {
            reader.realize({8, num_blocks});
        } catch (const Halide::RuntimeError &e) {
            error = true;
            if (string(e.what()).find(expected) == string::npos) {
                printf("Unexpected error reading lanes out of bounds (widen = %d):\n%s\n", widen, e.what());
                return 1;
            }
        }
        if (!error) {
            printf("Reading lanes out of the bounds hoist_invariants() assumed should fail "
                   "(widen = %d)!\n",
                   widen);
            return 1;
        }
    }
    return 0;
}

// Without a bound on lane, or with one that doesn't imply lane < 4, the factor
// isn't known to be invariant over kc. Only bound() with a min and an extent
// counts; bound_extent() leaves the min free.
int hoist_invariants_block_two_chunk_unbounded_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    for (int config = 0; config < 3; config++) {
        ImageParam data_p{Int(8), 1, "data_p"};
        ImageParam scale_p{Float(32), 1, "scale_p"};
        Var u{"u"}, lane{"lane"};
        Func out{"out"};
        Func blocks = block_two_chunk_reduction(data_p, scale_p, u, lane, out);
        if (config == 1) {
            blocks.bound_extent(lane, 4);
        } else if (config == 2) {
            blocks.bound(lane, 0, 8);
        }

        bool error = false;
        try {
            blocks.update().hoist_invariants();
        } catch (const Halide::CompileError &e) {
            error = true;
            const string expected =
                "hoist_invariants() could not find multiple reduction terms or a "
                "distributable loop-invariant factor in the update definition of " +
                blocks.name() + ".";
            if (string(e.what()).find(expected) == string::npos) {
                printf("Unexpected error for an unbounded lane (config %d):\n%s\n", config, e.what());
                return 1;
            }
        }
        if (!error) {
            printf("hoist_invariants should not hoist a factor that needs an unknown "
                   "lane range (config %d)!\n",
                   config);
            return 1;
        }
    }
    return 0;
}

// A per-block index may itself be data dependent, bounded only by a promise:
// scale(unsafe_promise_clamped(group(r / 32), 0, G - 1)). The promise must
// survive hoisting, as it is the only bound on the hoisted factor's access to
// scale. A user's promise around an RVar-dependent value is never dropped, so
// shift(unsafe_promise_clamped(base(r / 32) * 32 + r, 0, M - 1) / 32) stays in
// the intermediate, although it is invariant within a block.
int hoist_invariants_block_index_promise_test() {
    constexpr int block = 32, num_blocks = 3, num_groups = 4, n = block * num_blocks;
    constexpr int max_base = 2, num_shifts = max_base + num_blocks;
    ImageParam data_p{Int(8), 1, "data_p"};
    ImageParam group_p{Int(32), 1, "group_p"};
    ImageParam scale_p{Float(32), 1, "scale_p"};
    ImageParam base_p{Int(32), 1, "base_p"};
    ImageParam shift_p{Float(32), 1, "shift_p"};

    Var u{"u"};
    RDom r(0, n, "r");
    RVar ko{"ko"}, ki{"ki"};

    Func out{"out"};
    out() = 0.0f;
    out() += cast<float>(data_p(r)) *
             scale_p(unsafe_promise_clamped(group_p(r / block), 0, num_groups - 1)) *
             shift_p(unsafe_promise_clamped(base_p(r / block) * block + r, 0, num_shifts * block - 1) / block);
    out.update().split(r, ko, ki, block);
    Func blocks = out.update().rfactor(ko, u);
    Func blocks_intm = blocks.update().hoist_invariants();
    blocks.compute_root();
    blocks_intm.compute_root();

    internal_assert(!calls_func(blocks_intm.update_value(), Func(scale_p).name()))
        << "hoist_invariants promised block index: the factor was not hoisted: "
        << blocks_intm.update_value() << "\n";
    internal_assert(calls_func(blocks_intm.update_value(), Func(shift_p).name()))
        << "hoist_invariants promised block index: a promise on an RVar-dependent "
        << "index was dropped: " << blocks_intm.update_value() << "\n";

    Buffer<int8_t> data(n);
    Buffer<int> group(num_blocks), base(num_blocks);
    Buffer<float> scale(num_groups), shift(num_shifts);
    for (int k = 0; k < n; k++) {
        data(k) = (int8_t)((k * 3) % 7 - 3);
    }
    group(0) = 2;
    group(1) = 0;
    group(2) = 3;
    base(0) = 1;
    base(1) = 0;
    base(2) = max_base;
    for (int g = 0; g < num_groups; g++) {
        scale(g) = (float)(g + 1) * 0.5f;
    }
    for (int i = 0; i < num_shifts; i++) {
        shift(i) = (float)(i + 3);
    }
    data_p.set(data);
    group_p.set(group);
    scale_p.set(scale);
    base_p.set(base);
    shift_p.set(shift);

    Buffer<float> result = out.realize();

    float ref = 0.0f;
    for (int k = 0; k < n; k++) {
        ref += (float)data(k) * scale(group(k / block)) * shift((base(k / block) * block + k) / block);
    }
    internal_assert(result() == ref)
        << "hoist_invariants promised block index: "
        << result() << " vs ref " << ref << "\n";

    return 0;
}

// Per-block invariance applies to every distributive law, not just sum/product:
// a per-block bias under min/+ and a per-block mask under ||/&&.
int hoist_invariants_block_index_other_laws_test() {
    constexpr int block = 32, n = 100, num_blocks = (n + block - 1) / block;
    ImageParam data_p{Int(32), 1, "data_p"};
    ImageParam bias_p{Int(32), 1, "bias_p"};
    ImageParam mask_p{UInt(8), 1, "mask_p"};

    Var u{"u"};
    RDom r(0, data_p.dim(0).extent(), "r");
    RVar ko{"ko"}, ki{"ki"};

    Func lo{"lo"};
    lo() = Int(32).max();
    lo() = min(lo(), bias_p(r / block) + data_p(r));
    lo.update().split(r, ko, ki, block);
    Func lo_blocks = lo.update().rfactor(ko, u);
    Func lo_intm = lo_blocks.update().hoist_invariants();
    lo_blocks.compute_root();
    lo_intm.compute_root();
    internal_assert(!calls_func(lo_intm.update_value(), Func(bias_p).name()))
        << "hoist_invariants min block index: the bias was not hoisted: "
        << lo_intm.update_value() << "\n";

    Func any{"any"};
    any() = cast<bool>(false);
    any() = any() || (mask_p(r / block) != 0 && data_p(r) > 0);
    any.update().split(r, ko, ki, block);
    Func any_blocks = any.update().rfactor(ko, u);
    Func any_intm = any_blocks.update().hoist_invariants();
    any_blocks.compute_root();
    any_intm.compute_root();
    internal_assert(!calls_func(any_intm.update_value(), Func(mask_p).name()))
        << "hoist_invariants or block index: the mask was not hoisted: "
        << any_intm.update_value() << "\n";

    Buffer<int> data(n), bias(num_blocks);
    Buffer<uint8_t> mask(num_blocks);
    for (int k = 0; k < n; k++) {
        data(k) = -((k * 7) % 13);
    }
    for (int b = 0; b < num_blocks; b++) {
        bias(b) = 5 * b - 7;
        mask(b) = b % 2;
    }
    data_p.set(data);
    bias_p.set(bias);
    mask_p.set(mask);

    int lo_ref = std::numeric_limits<int>::max();
    for (int k = 0; k < n; k++) {
        lo_ref = std::min(lo_ref, bias(k / block) + data(k));
    }
    Buffer<int> lo_result = lo.realize();
    internal_assert(lo_result() == lo_ref)
        << "hoist_invariants min block index: " << lo_result() << " vs ref " << lo_ref << "\n";

    // data(10) > 0 lies in a block with mask 0; data(40) > 0 in one with mask 1.
    for (bool expected : {false, true}) {
        data(10) = 1;
        data(40) = expected ? 1 : 0;
        Buffer<bool> any_result = any.realize();
        internal_assert(any_result() == expected)
            << "hoist_invariants or block index: " << any_result() << " vs ref " << expected << "\n";
    }

    return 0;
}

// Only factors that are invariant for every value of the RVar bounds may be
// hoisted. With an RDom that starts at 8, the split index is 8 + u*32 + ki, so
// coarse(r / 32) changes value within a block and must stay in the
// intermediate, while the block-aligned aligned((r - 8) / 32) is hoisted.
int hoist_invariants_block_index_varying_test() {
    constexpr int block = 32, base = 8, n = 100;
    ImageParam data_p{Int(8), 1, "data_p"};
    ImageParam aligned_p{Float(32), 1, "aligned_p"};
    ImageParam coarse_p{Float(32), 1, "coarse_p"};

    Var u{"u"};
    RDom r(base, n, "r");
    RVar ko{"ko"}, ki{"ki"};

    Func out{"out"};
    out() = 0.0f;
    out() += cast<float>(data_p(r)) * aligned_p((r - base) / block) * coarse_p(r / block);
    out.update().split(r, ko, ki, block);
    Func blocks = out.update().rfactor(ko, u);
    Func blocks_intm = blocks.update().hoist_invariants();
    blocks.compute_root();
    blocks_intm.compute_root();

    internal_assert(!calls_func(blocks_intm.update_value(), Func(aligned_p).name()))
        << "hoist_invariants varying block index: the block-aligned factor was not hoisted: "
        << blocks_intm.update_value() << "\n";
    internal_assert(calls_func(blocks_intm.update_value(), Func(coarse_p).name()))
        << "hoist_invariants varying block index: a factor that varies within a block was hoisted: "
        << blocks_intm.update_value() << "\n";

    Buffer<int8_t> data(n);
    data.set_min(base);
    Buffer<float> aligned((n + block - 1) / block), coarse((base + n + block - 1) / block);
    for (int k = base; k < base + n; k++) {
        data(k) = (int8_t)((k * 5) % 9 - 4);
    }
    for (int b = 0; b < aligned.width(); b++) {
        aligned(b) = (float)(b + 2);
    }
    for (int b = 0; b < coarse.width(); b++) {
        coarse(b) = (float)(3 * b + 1) * 0.25f;
    }
    data_p.set(data);
    aligned_p.set(aligned);
    coarse_p.set(coarse);

    Buffer<float> result = out.realize();

    float ref = 0.0f;
    for (int k = base; k < base + n; k++) {
        ref += (float)data(k) * aligned((k - base) / block) * coarse(k / block);
    }
    internal_assert(result() == ref)
        << "hoist_invariants varying block index: "
        << result() << " vs ref " << ref << "\n";

    return 0;
}

// A factor indexed at a finer granularity than the split (r / 16 with blocks of
// 32) genuinely varies within a block, so there is nothing to hoist.
int hoist_invariants_block_index_finer_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    ImageParam data_p{Int(8), 1, "data_p"};
    ImageParam scale_p{Float(32), 1, "scale_p"};

    Var u{"u"};
    RDom r(0, data_p.dim(0).extent(), "r");
    RVar ko{"ko"}, ki{"ki"};

    Func out{"out"};
    out() = 0.0f;
    out() += cast<float>(data_p(r)) * scale_p(r / 16);
    out.update().split(r, ko, ki, 32);
    Func blocks = out.update().rfactor(ko, u);

    bool error = false;
    try {
        blocks.update().hoist_invariants();
    } catch (const Halide::CompileError &e) {
        error = true;
        const string expected =
            "hoist_invariants() could not find multiple reduction terms or a "
            "distributable loop-invariant factor in the update definition of " +
            blocks.name() + ".";
        if (string(e.what()).find(expected) == string::npos) {
            printf("Unexpected error for a factor that varies within a block:\n%s\n", e.what());
            return 1;
        }
    }
    if (!error) {
        printf("hoist_invariants should not hoist a factor that varies within a block!\n");
        return 1;
    }
    return 0;
}

// The min/max + add hoisting law is only valid for integer types where addition
// has no defined wraparound behavior. For UInt(8), hoisting the invariant 250
// would incorrectly turn min_k((250 + x_k) mod 256) into (250 + min_k(x_k)) mod
// 256, so hoist_invariants() must refuse rather than silently miscompile.
int hoist_invariants_invalid_law_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    Buffer<uint8_t> data(2);
    data(0) = 1;
    data(1) = 10;

    RDom k(0, 2, "k");

    Func f{"f"};
    f() = UInt(8).max();
    f() = min(f(), cast<uint8_t>(250) + data(k));

    bool error = false;
    try {
        f.update().hoist_invariants();
    } catch (const Halide::CompileError &e) {
        error = true;
        const string expected =
            "hoist_invariants() could not find multiple reduction terms or a "
            "distributable loop-invariant factor in the update definition of " +
            f.name() + ".";
        if (string(e.what()).find(expected) == string::npos) {
            printf("Unexpected error for unsigned min hoisting:\n%s\n", e.what());
            return 1;
        }
    }
    if (!error) {
        printf("hoist_invariants should have rejected the unsigned min law!\n");
        return 1;
    }
    return 0;
}

// hoist_invariants() errors when there is neither an invariant factor to hoist
// nor multiple outer terms to split, rather than behaving like plain rfactor().
int hoist_invariants_nothing_to_hoist_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }

    ImageParam data_p{Int(32), 2, "data_p"};
    Var i{"i"};
    RDom k(0, 8, "k");

    Func f{"f"};
    f(i) = 0;
    f(i) += data_p(i, k);

    bool error = false;
    try {
        f.update().hoist_invariants();
    } catch (const Halide::CompileError &e) {
        error = true;
        const string expected =
            "hoist_invariants() could not find multiple reduction terms or a "
            "distributable loop-invariant factor in the update definition of " +
            f.name() + ".";
        if (string(e.what()).find(expected) == string::npos) {
            printf("Unexpected error when no invariant is hoistable:\n%s\n", e.what());
            return 1;
        }
    }
    if (!error) {
        printf("hoist_invariants should have errored when there is nothing to hoist!\n");
        return 1;
    }
    return 0;
}

// In an integer sum, the right shift of a bit field (x >> s) & m at a
// position s invariant over the reduction is one more factor: the fields
// are read in place, x & (m << s), and the sum is shifted once, where that
// is exact. Planes of 2-bit fields, as GGML's q2_K and tq2_0: value i of 64
// is the field at bit 2 * (i / 16) of byte i % 16.
struct FieldInputs {
    static constexpr int Planes = 4, Positions = 16, Values = Planes * Positions;
    Buffer<uint8_t> wb{Positions};
    Buffer<int8_t> ab{Values};
    Func w{"w"}, a{"a"};
    FieldInputs() {
        for (int i = 0; i < Positions; i++) {
            wb(i) = (uint8_t)(i * 89 + 7);
        }
        wb(0) = 255;  // every field 3
        for (int i = 0; i < Values; i++) {
            ab(i) = (int8_t)(i * 37 + 11);
        }
        ab(0) = -128;
        Var v{"v"};
        w(v) = wb(v);
        a(v) = ab(v);
    }
    int field(int i) const {
        return (wb(i % Positions) >> (2 * (i / Positions))) & 3;
    }
    // The dot of plane c's fields with their a, scaled by 2^d.
    int plane_dot(int c, int d = 0) const {
        int sum = 0;
        for (int i = c * Positions; i < (c + 1) * Positions; i++) {
            sum += (field(i) << d) * ab(i);
        }
        return sum;
    }
};

bool check_shift(bool ok, const string &what) {
    if (!ok) {
        printf("%s\n", what.c_str());
    }
    return ok;
}

// The dot of the fields (as int8 codes) with an int8 Func, per plane after an
// rfactor() (c), as GGML's tq2_0 vec_dot: the shifts of the planes at bits 2
// and 4 hoist (their fields read in place, 12 and 48, fit an int8); bit 6's
// don't (192 doesn't), so it is read as before.
int hoist_invariants_field_shift_test(bool scheduled) {
    using In = FieldInputs;
    In in;
    RDom r(0, In::Values, "r");
    Func f{"f"};
    f() = 0;
    Expr field = (in.w(r % In::Positions) >> cast<uint8_t>(2 * (r / In::Positions))) & cast<uint8_t>(3);
    f() += cast<int>(widening_mul(cast<int8_t>(field), in.a(r)));
    RVar rp{"rp"}, ri{"ri"};
    Var c{"c"};
    f.update().split(r, rp, ri, In::Positions);
    Func planes = f.update().rfactor(rp, c);
    FuncVec intms = planes.update().hoist_invariants();
    if (!check_shift(intms.size() == 1, "field shift: expected one intermediate")) {
        return 1;
    }
    Func g = intms[0];
    if (scheduled) {
        planes.compute_root().unroll(c);
        planes.update().unroll(c);
        g.compute_at(planes, c);
        g.update().atomic().vectorize(ri);
    }
    int ref = 0;
    for (int p = 0; p < In::Planes; p++) {
        ref += in.plane_dot(p);
    }
    Buffer<int> out = f.realize();
    if (!check_shift(out() == ref, "field shift: wrong result")) {
        return 1;
    }
    if (scheduled) {
        return 0;
    }
    // The intermediate holds each plane's dot times 2^d.
    Buffer<int> gout = g.realize({In::Planes});
    const int d[In::Planes] = {0, 2, 4, 0};
    for (int p = 0; p < In::Planes; p++) {
        if (!check_shift(gout(p) == in.plane_dot(p, d[p]),
                         "field shift: the intermediate of plane " + std::to_string(p) + " is " +
                             std::to_string(gout(p)) + ", not " + std::to_string(in.plane_dot(p, d[p])))) {
            return 1;
        }
    }
    return 0;
}

// The sum over the domain must fit with the fields read in place: an int16
// sum of 16 products of a field in [0, 3] and an int8 fits in place at shift
// 2 (16 * 3 * 128 * 4 = 24576), not 4. The shift is hoisted along with an
// invariant factor s, and, given the groups, in the part calling a but not
// in the residual. The position 2 * c is invariant over r, but bounded
// only by c's bound.
int hoist_invariants_field_shift_sum_bound_test(bool groups) {
    using In = FieldInputs;
    In in;
    RDom r(0, In::Positions, "r");
    Var c{"c"};
    Func s{"s"};
    s(c) = cast<int16_t>(c - 2);
    Func f{"f"};
    f(c) = cast<int16_t>(0);
    Expr field = (in.w(r) >> cast<uint8_t>(2 * c)) & cast<uint8_t>(3);
    Expr term = s(c) * (cast<int16_t>(field) * cast<int16_t>(in.a(c * In::Positions + r)));
    f(c) += groups ? term + cast<int16_t>(in.w(r) & 1) : term;
    f.bound(c, 0, In::Planes);
    FuncVec intms = groups ? f.update().hoist_invariants({in.a}) : f.update().hoist_invariants();
    if (!check_shift(intms.size() == (groups ? 2 : 1) && intms[0].defined() && (!groups || intms[1].defined()),
                     "field shift sum bound: unexpected intermediates")) {
        return 1;
    }
    Buffer<int16_t> out = f.realize({In::Planes});
    Buffer<int16_t> gout = intms[0].realize({In::Planes});
    int residual_sum = 0;
    for (int i = 0; i < In::Positions; i++) {
        residual_sum += in.wb(i) & 1;
    }
    const int d[In::Planes] = {0, 2, 0, 0};
    for (int p = 0; p < In::Planes; p++) {
        const int expected = (int16_t)((p - 2) * in.plane_dot(p) + (groups ? residual_sum : 0));
        if (!check_shift(out(p) == expected, "field shift sum bound: wrong result for plane " + std::to_string(p)) ||
            !check_shift(gout(p) == in.plane_dot(p, d[p]),
                         "field shift sum bound: wrong intermediate of plane " + std::to_string(p))) {
            return 1;
        }
    }
    return 0;
}

// As tq2_0's planes: the position by a middle split of the domain, value i
// of 128 at plane (i % 64) / 16, i.e. ((ro * 4 + c) * 16 + ri) % 64 / 16
// after the split and rfactor(), which is c only given c's bound (without
// which nothing is hoisted).
int hoist_invariants_field_shift_split_test() {
    using In = FieldInputs;
    In in;
    constexpr int Halves = 2;
    RDom r(0, Halves * In::Values, "r");
    auto make = [&](bool bounded, Func &planes) {
        Func f{"f"};
        f() = 0;
        Expr i = r % In::Values;
        Expr field = (in.w(i % In::Positions) >> cast<uint8_t>(2 * (i / In::Positions))) & cast<uint8_t>(3);
        f() += cast<int>(widening_mul(cast<int8_t>(field), in.a(i)));
        RVar ra{"ra"}, ro{"ro"}, rc{"rc"}, ri{"ri"};
        Var c{"c"};
        f.update().split(r, ra, ri, In::Positions).split(ra, ro, rc, In::Planes);
        planes = f.update().rfactor(rc, c);
        if (bounded) {
            planes.bound(c, 0, In::Planes);
        }
        return f;
    };
    Func planes;
    Func unbounded = make(false, planes);
    if (Halide::exceptions_enabled()) {
        bool error = false;
        try {
            planes.update().hoist_invariants();
        } catch (const Halide::CompileError &e) {
            error = string(e.what()).find("could not find multiple reduction terms") != string::npos;
        }
        if (!check_shift(error, "field shift split: hoisted without c's bound")) {
            return 1;
        }
    }
    Func f = make(true, planes);
    Func g = planes.update().hoist_invariants()[0];
    int ref = 0;
    for (int p = 0; p < In::Planes; p++) {
        ref += Halves * in.plane_dot(p);
    }
    Buffer<int> out = f.realize();
    if (!check_shift(out() == ref, "field shift split: wrong result")) {
        return 1;
    }
    Buffer<int> gout = g.realize({In::Planes});
    const int d[In::Planes] = {0, 2, 4, 0};
    for (int p = 0; p < In::Planes; p++) {
        if (!check_shift(gout(p) == Halves * in.plane_dot(p, d[p]),
                         "field shift split: wrong intermediate of plane " + std::to_string(p))) {
            return 1;
        }
    }
    return 0;
}

// With an invariant factor hoisted, the term's body is CSE'd, so the fields
// may be read under Lets (here of the index c * 16 + r): the shift is still
// hoisted when its position is invariant, and not when it depends on r.
int hoist_invariants_field_shift_let_test() {
    using In = FieldInputs;
    In in;
    RDom r(0, In::Positions, "r");
    Var c{"c"};
    Func s{"s"};
    s(c) = c - 2;
    auto make = [&](const string &name, const std::function<Expr(Expr)> &position) {
        Func f{name};
        Expr i = c * In::Positions + r;
        Expr field = (in.w(i % In::Positions) >> cast<uint8_t>(position(i))) & cast<uint8_t>(3);
        f(c) = 0;
        f(c) += s(c) * cast<int>(widening_mul(cast<int8_t>(field), in.a(i)));
        f.bound(c, 0, In::Planes);
        return f;
    };
    Func f = make("f", [&](const Expr &) { return 2 * c; });
    Func g = f.update().hoist_invariants()[0];
    Buffer<int> out = f.realize({In::Planes});
    Buffer<int> gout = g.realize({In::Planes});
    const int d[In::Planes] = {0, 2, 4, 0};
    for (int p = 0; p < In::Planes; p++) {
        if (!check_shift(out(p) == (p - 2) * in.plane_dot(p),
                         "field shift let: wrong result for plane " + std::to_string(p)) ||
            !check_shift(gout(p) == in.plane_dot(p, d[p]),
                         "field shift let: the intermediate of plane " + std::to_string(p) + " is " +
                             std::to_string(gout(p)) + ", not " + std::to_string(in.plane_dot(p, d[p])))) {
            return 1;
        }
    }
    // Hoisting s(c) again leaves the variant shift in place.
    Func h = make("h", [&](const Expr &i) { return i / 4 % 4 * 2; });
    Func hg = h.update().hoist_invariants()[0];
    Buffer<int> hout = h.realize({In::Planes});
    Buffer<int> hgout = hg.realize({In::Planes});
    for (int p = 0; p < In::Planes; p++) {
        int dot = 0;
        for (int j = 0; j < In::Positions; j++) {
            const int i = p * In::Positions + j;
            dot += ((in.wb(j) >> (i / 4 % 4 * 2)) & 3) * in.ab(i);
        }
        if (!check_shift(hout(p) == (p - 2) * dot && hgout(p) == dot,
                         "field shift let: wrong variant result for plane " + std::to_string(p))) {
            return 1;
        }
    }
    return 0;
}

// Sums whose shifts are not hoisted: there is then nothing to hoist.
int hoist_invariants_field_shift_rejected_test() {
    if (!Halide::exceptions_enabled()) {
        return 0;
    }
    using In = FieldInputs;
    In in;
    RDom r(0, In::Positions, "r");
    Var c{"c"};
    auto field = [&](const Expr &s, int m = 3) {
        return (in.w(r) >> cast<uint8_t>(s)) & cast<uint8_t>(m);
    };
    auto i8 = [](const Expr &e) { return cast<int8_t>(e); };
    auto i32 = [](const Expr &e) { return cast<int>(e); };
    auto rejected = [&](const string &name, const Expr &increment, bool use_max = false) {
        Func f{name};
        f(c) = cast(increment.type(), 0);
        if (use_max) {
            f(c) = max(f(c), increment);
        } else {
            f(c) += increment;
        }
        try {
            f.update().hoist_invariants();
        } catch (const Halide::CompileError &e) {
            return check_shift(string(e.what()).find("could not find multiple reduction terms") != string::npos,
                               name + ": unexpected error:\n" + e.what());
        }
        printf("%s: hoisted a shift it should not have\n", name.c_str());
        return false;
    };
    bool ok = true;
    // The position depends on r.
    ok &= rejected("f1", i32(field(2 * (r / 4))));
    // Not linear in the fields.
    ok &= rejected("f2", i32(field(2 * c)) * i32(field(2 * c)));
    // (A cast of a sum: one term, not a sum to split.)
    ok &= rejected("f3", i32(cast<int16_t>(field(2 * c)) + 1));
    // Fields at different positions.
    ok &= rejected("f4", i32(cast<int16_t>(field(2 * c)) + cast<int16_t>(field(2 * c + 1))));
    // A field of 8 bits doesn't fit the int8 even shifted down.
    ok &= rejected("f5", i32(widening_mul(i8(field(c, 255)), in.a(r))));
    // 3 << 6 doesn't fit the int8 (at most 3 << 5 does).
    ok &= rejected("f6", i32(widening_mul(i8(field(6)), in.a(r))));
    ok &= rejected("f7", cast<float>(field(2 * c)));
    ok &= rejected("f8", i32(field(2 * c)), true);
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv) {
    struct Task {
        std::string desc;
        std::function<int()> fn;
    };

    std::vector<Task> tasks = {
        {"hoist_invariants test (add/mul)", hoist_invariants_test},
        {"hoist_invariants test (add/mul, scattered factors)", hoist_invariants_scattered_factors_test},
        {"hoist_invariants test (add/mul, scattered factors, unsigned)", hoist_invariants_scattered_factors_unsigned_test},
        {"hoist_invariants test (multiple terms)", hoist_invariants_multiple_terms_test},
        {"hoist_invariants test (unscaled terms)", hoist_invariants_unscaled_terms_test},
        {"hoist_invariants test (tuple terms)", hoist_invariants_tuple_terms_test},
        {"hoist_invariants test (min/add)", hoist_invariants_min_test},
        {"hoist_invariants test (or/and)", hoist_invariants_or_test},
        {"hoist_invariants test (strict_float preserved)", hoist_invariants_strict_float_test},
        {"hoist_invariants test (predicated RDom)", hoist_invariants_predicated_rdom_test},
        {"hoist_invariants test (predicated RDom, RVar-free term)", hoist_invariants_predicated_rvar_free_term_test},
        {"hoist_invariants test (after rfactor)", hoist_invariants_after_rfactor_test},
        {"hoist_invariants test (block index, divisible)", [] { return hoist_invariants_block_index_test(false, 128); }},
        {"hoist_invariants test (block index, tail)", [] { return hoist_invariants_block_index_test(false, 100); }},
        {"hoist_invariants test (block index, constant tail)", [] { return hoist_invariants_block_index_test(true, 100); }},
        {"hoist_invariants test (block lane, divisible)", [] { return hoist_invariants_block_lane_test(128); }},
        {"hoist_invariants test (block lane, tail)", [] { return hoist_invariants_block_lane_test(100); }},
        {"hoist_invariants test (two chunks, bound lane)", [] { return hoist_invariants_block_two_chunk_test(128); }},
        {"hoist_invariants test (two chunks, bound lane, tail)", [] { return hoist_invariants_block_two_chunk_test(100); }},
        {"hoist_invariants test (two chunks, lane not bounded)", hoist_invariants_block_two_chunk_unbounded_test},
        {"hoist_invariants test (two chunks, lanes out of bounds)", hoist_invariants_block_two_chunk_out_of_bounds_test},
        {"hoist_invariants test (block index, promised)", hoist_invariants_block_index_promise_test},
        {"hoist_invariants test (block index, min/add and or/and)", hoist_invariants_block_index_other_laws_test},
        {"hoist_invariants test (block index, varying)", hoist_invariants_block_index_varying_test},
        {"hoist_invariants test (block index, finer factor rejected)", hoist_invariants_block_index_finer_rejected_test},
        {"hoist_invariants test (invalid law rejected)", hoist_invariants_invalid_law_rejected_test},
        {"hoist_invariants test (nothing to hoist rejected)", hoist_invariants_nothing_to_hoist_rejected_test},
        {"hoist_invariants test (field shift)", [] { return hoist_invariants_field_shift_test(false); }},
        {"hoist_invariants test (field shift, scheduled)", [] { return hoist_invariants_field_shift_test(true); }},
        {"hoist_invariants test (field shift, sum bound)", [] { return hoist_invariants_field_shift_sum_bound_test(false); }},
        {"hoist_invariants test (field shift, sum bound, groups)", [] { return hoist_invariants_field_shift_sum_bound_test(true); }},
        {"hoist_invariants test (field shift, split classes)", hoist_invariants_field_shift_split_test},
        {"hoist_invariants test (field shift, rejected)", hoist_invariants_field_shift_rejected_test},
        {"hoist_invariants test (field shift, let)", hoist_invariants_field_shift_let_test},
    };

    using Sharder = Halide::Internal::Test::Sharder;
    Sharder sharder;
    for (size_t t = 0; t < tasks.size(); t++) {
        if (!sharder.should_run(t)) continue;
        const auto &task = tasks.at(t);
        std::cout << task.desc << "\n";
        if (task.fn() != 0) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
