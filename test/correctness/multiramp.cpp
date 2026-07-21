#include "Halide.h"

#include <cstdio>
#include <set>
#include <vector>

using namespace Halide;
using namespace Halide::Internal;

namespace {

int failures = 0;

// Expand a MultiRamp (with const base and const strides) to a flat vector
// using the same innermost-fastest enumeration the IR uses.
std::vector<int> expand(const MultiRamp &m) {
    auto cb = as_const_int(simplify(m.base));
    internal_assert(cb) << "expand() only supports const bases, got " << m.base << "\n";
    int64_t b = *cb;
    std::vector<int64_t> strides;
    for (const Expr &s : m.strides) {
        auto cs = as_const_int(simplify(s));
        internal_assert(cs) << "expand() only supports const strides, got " << s << "\n";
        strides.push_back(*cs);
    }
    int total = 1;
    for (int n : m.lanes) {
        total *= n;
    }
    std::vector<int> result;
    result.reserve(total);
    for (int flat = 0; flat < total; flat++) {
        int rem = flat;
        int64_t v = b;
        for (size_t i = 0; i < m.lanes.size(); i++) {
            int idx = rem % m.lanes[i];
            rem /= m.lanes[i];
            v += strides[i] * idx;
        }
        result.push_back((int)v);
    }
    return result;
}

void print_vec(const std::vector<int> &v) {
    printf("[");
    for (size_t i = 0; i < v.size(); i++) {
        printf("%s%d", i ? ", " : "", v[i]);
    }
    printf("]");
}

void check_seq(const std::vector<int> &got, const std::vector<int> &want,
               const char *msg, int line) {
    if (got != want) {
        printf("FAIL at %d: %s\n  got ", line, msg);
        print_vec(got);
        printf("\n  want ");
        print_vec(want);
        printf("\n");
        failures++;
    }
}

#define CHECK(cond, msg)                               \
    do {                                               \
        if (!(cond)) {                                 \
            printf("FAIL at %d: %s\n", __LINE__, msg); \
            failures++;                                \
        }                                              \
    } while (0)

#define CHECK_SEQ_LIT(got, msg, ...) check_seq((got), std::vector<int>{__VA_ARGS__}, (msg), __LINE__)
#define CHECK_SEQ(got, want, msg) check_seq((got), (want), (msg), __LINE__)

// ---- MultiRamp::add ------------------------------------------------------

void check_add_refinable_shapes() {
    // From the math problem: A = ramp(0,1,6) = [0,1,2,3,4,5],
    //                        B = ramp(ramp(0,2,2),100,3) = [0,2,100,102,200,202],
    //                   A + B = [0,3,102,105,204,207].
    // Shapes (6,) and (2,3) (innermost first) must refine to (2,3).
    MultiRamp A{0, {1}, {6}};
    MultiRamp B{0, {2, 100}, {2, 3}};
    CHECK(A.add(B), "add with refinable shapes");
    CHECK_SEQ_LIT(expand(A), "refinable-shape add values", 0, 3, 102, 105, 204, 207);
}

void check_add_same_shape() {
    MultiRamp A{10, {3, 100}, {4, 2}};
    MultiRamp B{5, {-1, 50}, {4, 2}};
    auto a_seq = expand(A), b_seq = expand(B);
    CHECK(A.add(B), "same-shape add");
    std::vector<int> want(8);
    for (size_t i = 0; i < a_seq.size(); i++) {
        want[i] = a_seq[i] + b_seq[i];
    }
    CHECK_SEQ(expand(A), want, "same-shape add values");
}

void check_add_incompatible_shapes() {
    // Shapes with innermost sizes 3 vs 2 and outer sizes 2 vs 3 can't refine.
    MultiRamp A{0, {1, 100}, {3, 2}};
    MultiRamp B{0, {1, 100}, {2, 3}};
    CHECK(!A.add(B), "incompatible shapes rejected");
}

void check_add_cancels_to_zero() {
    // 2·A + (-2)·A should simplify to a single zero-stride dim (one flat dim
    // of the total lane count).
    MultiRamp A{7, {3, 100}, {4, 2}};
    MultiRamp B = A;
    A.mul(2);
    B.mul(-2);
    CHECK(A.add(B), "add of cancelling multiramps");
    CHECK(A.lanes.size() == 1, "cancelled add should collapse to 1 dim");
    if (A.lanes.size() == 1) {
        CHECK(A.lanes[0] == 8, "cancelled add lanes = 8");
        auto s = as_const_int(simplify(A.strides[0]));
        CHECK(s && *s == 0, "cancelled add stride = 0");
        auto b = as_const_int(simplify(A.base));
        CHECK(b && *b == 0, "cancelled add base = 0");
    }
}

void check_add_scaled_outer() {
    // Regression test for the stride-scaling bug: adding a 1D ramp of length 6
    // to a 2D ramp with shape (2,3) must scale the 1D's stride by 2 when
    // producing the outer dim of the result.
    //   A = ramp(0,1,6)          -> [0,1,2,3,4,5]
    //   B = ramp(ramp(0,0,2),100,3) = broadcast(0,2) then + ramp-of-100s
    //                             -> [0,0,100,100,200,200]
    //   A+B = [0,1,102,103,204,205]
    MultiRamp A{0, {1}, {6}};
    MultiRamp B{0, {0, 100}, {2, 3}};
    CHECK(A.add(B), "scaled-outer add");
    CHECK_SEQ_LIT(expand(A), "scaled-outer values", 0, 1, 102, 103, 204, 205);
}

// ---- MultiRamp::div -----------------------------------------------------

void check_div_pure_carry_const() {
    MultiRamp A{8, {4, 12}, {2, 3}};
    auto a_seq = expand(A);
    CHECK(A.div(4), "pure-carry div (const k)");
    std::vector<int> want(a_seq.size());
    for (size_t i = 0; i < a_seq.size(); i++) {
        want[i] = a_seq[i] / 4;
    }
    CHECK_SEQ(expand(A), want, "pure-carry div values");
}

void check_div_symbolic_strides() {
    // Symbolic base and strides, all provably multiples of the denominator —
    // every dim is pure carry.
    Var v("v");
    MultiRamp A{2 * v, {2 * v, 8 * v}, {4, 5}};
    CHECK(A.div(2), "pure-carry div with symbolic strides");
    if (A.strides.size() == 2) {
        // Strides become (2*v/2, 8*v/2) = (v, 4*v).
        Expr want0 = simplify(A.strides[0] - v);
        Expr want1 = simplify(A.strides[1] - 4 * v);
        CHECK(is_const_zero(want0), "sym-stride div inner");
        CHECK(is_const_zero(want1), "sym-stride div outer");
    }
}

void check_div_merges_adjacent_pure_carry() {
    // Two pure-carry input dims whose output strides line up should collapse
    // into a single output dim.
    // Input values: 0, 4, 8, 12, 16, 20 (strides [4, 12], lanes [3, 2]).
    // Divided by 4: 0, 1, 2, 3, 4, 5 — a flat 1D ramp of length 6.
    MultiRamp A{0, {4, 12}, {3, 2}};
    CHECK(A.div(4), "div of two pure-carry dims");
    CHECK(A.lanes.size() == 1, "adjacent dims should merge into one");
    if (A.lanes.size() == 1) {
        CHECK(A.lanes[0] == 6, "merged lane count");
    }
    CHECK_SEQ_LIT(expand(A), "merged values", 0, 1, 2, 3, 4, 5);
}

void check_div_with_split() {
    // ramp(0,2,6) / 4 = [0,0,1,1,2,2], needs a split of dim 6 -> (2,3).
    MultiRamp A{0, {2}, {6}};
    CHECK(A.div(4), "div with split");
    CHECK_SEQ_LIT(expand(A), "split div values", 0, 0, 1, 1, 2, 2);
}

void check_div_split_with_symbolic_stride() {
    // Non-constant stride whose residue mod k is still pinned down: stride
    // is 4*v + 2, which is always ≡ 2 (mod 4). The split needs p=2, which
    // divides 6. The budget check uses r = 2 only.
    Var v("v");
    MultiRamp A{0, {4 * v + 2}, {6}};
    CHECK(A.div(4), "div split with symbolic stride");
    // Expected shape after split: lanes (2, 3); inner stride = (4v+2)/4
    // (symbolic), outer stride = (4v+2)*2/4 = 2v+1.
    CHECK(A.lanes.size() == 2, "split produced two output dims");
    if (A.lanes.size() == 2) {
        CHECK(A.lanes[0] == 2 && A.lanes[1] == 3, "split lanes (2, 3)");
        // Outer stride should simplify to 2v + 1.
        Expr outer = simplify(A.strides[1]);
        Expr want = simplify(2 * v + 1);
        CHECK(equal(outer, want), "outer stride is 2v+1");
    }
}

void check_div_rejects_non_multiramp() {
    // ramp(0,1,5)/2 = [0,0,1,1,2], not a multiramp (5 has no usable factor).
    MultiRamp A{0, {1}, {5}};
    CHECK(!A.div(2), "should reject ramp(0,1,5)/2");
}

void check_div_rejects_unaligned_base() {
    // ramp(2,2,6)/4 = [0,1,1,2,2,3] would be a multiramp, but our algorithm
    // requires the base to be a known multiple of the denominator, and 2 is
    // not a multiple of 4.
    MultiRamp A{2, {2}, {6}};
    CHECK(!A.div(4), "should reject div when base isn't aligned");
}

void check_div_rejects_symbolic_denominator() {
    // A symbolic (non-constant) denominator should fail cleanly. The code
    // needs k as a known positive integer to reason about bucket sizes.
    Var k("k");
    MultiRamp A{0, {1}, {4}};
    CHECK(!A.div(k), "should reject div with symbolic denominator");
    CHECK(!A.mod(k), "should reject mod with symbolic denominator");
}

// ---- MultiRamp::mod -----------------------------------------------------

void check_mod_basic() {
    MultiRamp A{0, {1}, {6}};
    CHECK(A.mod(2), "mod basic");
    CHECK_SEQ_LIT(expand(A), "mod basic values", 0, 1, 0, 1, 0, 1);
}

void check_mod_with_split() {
    MultiRamp A{0, {2}, {6}};
    CHECK(A.mod(4), "mod with split");
    CHECK_SEQ_LIT(expand(A), "mod split values", 0, 2, 0, 2, 0, 2);
}

void check_mod_symbolic_strides() {
    // Symbolic base and strides, all provably multiples of the denominator:
    // mod result is entirely zero.
    Var v("v");
    MultiRamp A{2 * v, {6 * v, 10 * v}, {3, 2}};
    CHECK(A.mod(2), "mod pure-carry symbolic strides");
    for (const Expr &s : A.strides) {
        auto c = as_const_int(simplify(s));
        CHECK(c && *c == 0, "sym-stride mod stride = 0");
    }
    auto b = as_const_int(simplify(A.base));
    CHECK(b && *b == 0, "sym-stride mod base = 0");
}

void check_mod_rejects_non_multiramp() {
    // ramp(0,1,5)%2 = [0,1,0,1,0], not a multiramp.
    MultiRamp A{0, {1}, {5}};
    CHECK(!A.mod(2), "should reject ramp(0,1,5)%2");
}

// ---- End-to-end is_multiramp tests --------------------------------------

void check_recognize_1d_ramp() {
    Expr e = Ramp::make(Expr(0), Expr(2), 4);
    Scope<Expr> scope;
    MultiRamp m;
    CHECK(is_multiramp(e, scope, &m), "recognize 1D ramp");
    if (m.lanes.size() == 1) {
        CHECK(m.lanes[0] == 4, "1D lanes");
    }
}

void check_recognize_nested_ramp() {
    // ramp(ramp(0,1,2), broadcast(100,2), 3) -> strides [1,100], lanes [2,3].
    Expr inner = Ramp::make(Expr(0), Expr(1), 2);
    Expr e = Ramp::make(inner, Broadcast::make(Expr(100), 2), 3);
    Scope<Expr> scope;
    MultiRamp m;
    CHECK(is_multiramp(e, scope, &m), "recognize nested ramp");
    if (m.lanes.size() == 2) {
        CHECK(m.lanes[0] == 2 && m.lanes[1] == 3, "nested ramp lanes");
    }
}

void check_recognize_add() {
    Expr a = Ramp::make(Expr(0), Expr(1), 6);
    Expr inner = Ramp::make(Expr(0), Expr(2), 2);
    Expr b = Ramp::make(inner, Broadcast::make(Expr(100), 2), 3);
    Expr sum = Add::make(a, b);
    Scope<Expr> scope;
    MultiRamp m;
    CHECK(is_multiramp(sum, scope, &m), "recognize add of two multiramps");
}

void check_recognize_div_const() {
    Expr e = Div::make(Ramp::make(Expr(0), Expr(2), 6),
                       Broadcast::make(Expr(4), 6));
    Scope<Expr> scope;
    MultiRamp m;
    CHECK(is_multiramp(e, scope, &m), "recognize ramp/const");
    CHECK_SEQ_LIT(expand(m), "recognized div values", 0, 0, 1, 1, 2, 2);
}

void check_recognize_mod_const() {
    Expr e = Mod::make(Ramp::make(Expr(0), Expr(1), 6),
                       Broadcast::make(Expr(2), 6));
    Scope<Expr> scope;
    MultiRamp m;
    CHECK(is_multiramp(e, scope, &m), "recognize ramp%const");
    CHECK_SEQ_LIT(expand(m), "recognized mod values", 0, 1, 0, 1, 0, 1);
}

void check_recognize_div_symbolic_strides() {
    // (2*x) + ramp(0, 4, 4), divided by 2. Numerator has symbolic base, const
    // strides that are multiples of 2.
    Var x("x");
    Expr num = Broadcast::make(2 * x, 4) + Ramp::make(Expr(0), Expr(4), 4);
    Expr e = Div::make(num, Broadcast::make(Expr(2), 4));
    Scope<Expr> scope;
    MultiRamp m;
    CHECK(is_multiramp(e, scope, &m), "recognize symbolic-strides div");
    if (m.strides.size() == 1) {
        auto s = as_const_int(simplify(m.strides[0]));
        CHECK(s && *s == 2, "symbolic-strides div stride = 2");
    }
}

// ---- Reordering and shuffle_from_permuted -------------------------------

void check_reorder() {
    // Swap the two dims of a 2D multiramp.
    // base 0, strides [1, 10], lanes [2, 3]:   0,  1, 10, 11, 20, 21
    // reordered [1, 0] -> strides [10, 1], lanes [3, 2]:  0, 10, 20,  1, 11, 21
    MultiRamp A{0, {1, 10}, {2, 3}};
    MultiRamp R = A;
    R.reorder({1, 0});
    CHECK(R.lanes.size() == 2, "reordered dims");
    if (R.lanes.size() == 2) {
        CHECK(R.lanes[0] == 3 && R.lanes[1] == 2, "reordered lane counts");
        auto s0 = as_const_int(simplify(R.strides[0]));
        auto s1 = as_const_int(simplify(R.strides[1]));
        CHECK(s0 && *s0 == 10, "reordered stride 0");
        CHECK(s1 && *s1 == 1, "reordered stride 1");
    }
    CHECK_SEQ_LIT(expand(R), "reordered values", 0, 10, 20, 1, 11, 21);
}

void check_shuffle_from_permuted_2d() {
    // A has 2 dims; perm = [1, 0] swaps them. The shuffle takes the
    // permuted lane order back to the original lane order.
    MultiRamp A{0, {1, 10}, {2, 3}};
    MultiRamp P = A;
    P.reorder({1, 0});
    std::vector<int> idx = A.shuffle_from_permuted({1, 0});
    // For each output lane n (A's order), idx[n] is the input lane in P's
    // order that carries the same value.
    auto a_seq = expand(A);  // 0, 1, 10, 11, 20, 21
    auto p_seq = expand(P);  // 0, 10, 20, 1, 11, 21
    CHECK(idx.size() == a_seq.size(), "shuffle indices size");
    for (size_t n = 0; n < a_seq.size(); n++) {
        CHECK(p_seq[idx[n]] == a_seq[n], "shuffle restores original lane");
    }
    // And as a vector: [0, 3, 1, 4, 2, 5].
    std::vector<int> want = {0, 3, 1, 4, 2, 5};
    CHECK(idx == want, "shuffle indices match expected");
}

void check_shuffle_from_permuted_identity() {
    // perm = identity => indices = [0, 1, 2, ..., total_lanes-1].
    MultiRamp A{0, {1, 10, 100}, {2, 3, 4}};
    std::vector<int> idx = A.shuffle_from_permuted({0, 1, 2});
    for (size_t n = 0; n < idx.size(); n++) {
        CHECK((int)n == idx[n], "identity permutation indices");
    }
}

void check_shuffle_from_permuted_3d() {
    // 3D with cyclic permutation. Check by comparing expanded sequences.
    // base 0, strides [1, 4, 20], lanes [2, 3, 2]. Values:
    //   i_0 + 4*i_1 + 20*i_2 for (i_0, i_1, i_2) in [2)x[3)x[2).
    MultiRamp A{0, {1, 4, 20}, {2, 3, 2}};
    std::vector<int> perm = {2, 0, 1};  // outermost becomes innermost
    MultiRamp P = A;
    P.reorder(perm);
    std::vector<int> idx = A.shuffle_from_permuted(perm);
    auto a_seq = expand(A);
    auto p_seq = expand(P);
    CHECK(idx.size() == a_seq.size(), "3D shuffle size");
    for (size_t n = 0; n < a_seq.size(); n++) {
        CHECK(p_seq[idx[n]] == a_seq[n], "3D shuffle restores original");
    }
}

void check_shuffle_from_slice_2d() {
    // A has 2 dims, lanes [2, 3]. Slice dim 1 at pos 2 should yield lanes
    // [2]; the shuffle indices pick those lanes of A.
    MultiRamp A{0, {1, 10}, {2, 3}};
    MultiRamp S = A;
    S.slice(1, Expr(2));
    std::vector<int> idx = A.shuffle_from_slice(std::vector<int>{1}, std::vector<int>{2});
    auto a_seq = expand(A);  // 0, 1, 10, 11, 20, 21
    auto s_seq = expand(S);  // 20, 21
    CHECK(idx.size() == s_seq.size(), "slice shuffle size");
    for (size_t n = 0; n < s_seq.size(); n++) {
        CHECK(a_seq[idx[n]] == s_seq[n], "slice shuffle picks right lanes");
    }
    std::vector<int> want = {4, 5};
    CHECK(idx == want, "slice shuffle indices match expected");
}

void check_shuffle_from_slice_inner() {
    // Slice the innermost dim.
    MultiRamp A{0, {1, 10}, {2, 3}};
    MultiRamp S = A;
    S.slice(0, Expr(1));
    std::vector<int> idx = A.shuffle_from_slice(std::vector<int>{0}, std::vector<int>{1});
    auto a_seq = expand(A);  // 0, 1, 10, 11, 20, 21
    auto s_seq = expand(S);  // 1, 11, 21
    CHECK(idx.size() == s_seq.size(), "inner slice shuffle size");
    for (size_t n = 0; n < s_seq.size(); n++) {
        CHECK(a_seq[idx[n]] == s_seq[n], "inner slice picks right lanes");
    }
    std::vector<int> want = {1, 3, 5};
    CHECK(idx == want, "inner slice indices match expected");
}

void check_shuffle_from_slice_3d() {
    // 3D: strides [1, 4, 20], lanes [2, 3, 2]. Slice middle dim at pos 1.
    MultiRamp A{0, {1, 4, 20}, {2, 3, 2}};
    MultiRamp S = A;
    S.slice(1, Expr(1));
    std::vector<int> idx = A.shuffle_from_slice(std::vector<int>{1}, std::vector<int>{1});
    auto a_seq = expand(A);
    auto s_seq = expand(S);
    CHECK(idx.size() == s_seq.size(), "3D slice shuffle size");
    for (size_t n = 0; n < s_seq.size(); n++) {
        CHECK(a_seq[idx[n]] == s_seq[n], "3D slice picks right lanes");
    }
}

// ---- MultiRamp::mul ------------------------------------------------------

void check_mul_basic() {
    MultiRamp A{3, {1, 10}, {2, 3}};  // 3, 4, 13, 14, 23, 24
    A.mul(5);
    CHECK_SEQ_LIT(expand(A), "mul values", 15, 20, 65, 70, 115, 120);
}

// ---- MultiRamp::operator== ----------------------------------------------

void check_equality_same() {
    MultiRamp A{0, {1, 10}, {2, 3}};
    Expr e = simplify(A == A);
    CHECK(is_const_one(e), "multiramp equals itself");
}

void check_equality_different() {
    MultiRamp A{0, {1, 10}, {2, 3}};
    MultiRamp B{0, {1, 10}, {3, 2}};  // same total lanes, different shape
    // A.to_expr() == [0,1,10,11,20,21], B.to_expr() = [0,1,2,10,11,12];
    // so they are not equal in every lane.
    Expr e = simplify(A == B);
    CHECK(is_const_zero(e), "different multiramps compare false");
}

void check_equality_scalar() {
    MultiRamp A{42, {}, {}};
    MultiRamp B{42, {}, {}};
    MultiRamp C{7, {}, {}};
    CHECK(is_const_one(simplify(A == B)), "scalar multiramp equality");
    CHECK(is_const_zero(simplify(A == C)), "scalar multiramp inequality");
}

// ---- MultiRamp::alias_free_slice ----------------------------------------

void check_alias_free_slice_all_unique() {
    // All lanes of the returned slice should be unique.
    MultiRamp A{5, {1, 16}, {4, 3}};  // clearly alias-free
    auto peeled = A.alias_free_slice();
    CHECK(peeled.empty(), "fully alias-free: nothing peeled");
    auto seq = expand(A);
    std::set<int> unique(seq.begin(), seq.end());
    CHECK(unique.size() == seq.size(), "slice lanes are unique");
}

void check_alias_free_slice_peels_zero_stride() {
    // Stride-zero inner dim must be peeled.
    MultiRamp A{0, {0, 1}, {4, 5}};
    auto peeled = A.alias_free_slice();
    CHECK(peeled.size() == 1, "peeled the stride-zero dim");
    if (peeled.size() == 1) {
        CHECK(peeled[0].dim == 0 && peeled[0].lanes == 4,
              "peeled the right dim");
        CHECK(is_const_zero(peeled[0].stride), "peeled dim had stride zero");
    }
    // Remaining is {base=0, strides=[1], lanes=[5]} — unique.
    auto seq = expand(A);
    std::set<int> unique(seq.begin(), seq.end());
    CHECK(unique.size() == seq.size(), "remaining slice is unique");
}

void check_alias_free_slice_degenerate() {
    // A 1-dim ramp with stride zero: only dim is a duplication. It should
    // be peeled, leaving *this as a 0-dim scalar.
    MultiRamp A{7, {0}, {4}};
    auto peeled = A.alias_free_slice();
    CHECK(peeled.size() == 1, "peeled the only dim");
    CHECK(A.dimensions() == 0, "remaining is scalar");
    auto seq = expand(A);
    CHECK(seq.size() == 1 && seq[0] == 7, "scalar lane is base");
}

// ---- MultiRamp::rotate_stride_one_innermost -----------------------------

void check_rotate_stride_one_innermost() {
    // Stride-1 dim not innermost: rotating should produce a MultiRamp
    // whose expand, when transposed with cols = total / A, matches the
    // original expand.
    MultiRamp A{0, {10, 1}, {3, 4}};  // [0,10,20,1,11,21,2,12,22,3,13,23]
    auto orig = expand(A);
    int a = A.rotate_stride_one_innermost();
    CHECK(a > 0, "rotated (stride-1 was not innermost)");
    auto rotated = expand(A);
    // Per the header: make_transpose(rotated, total/a) recovers orig.
    // make_transpose(v, cols): output[j*rows + i] = v[i*cols + j], with
    // rows = v.size()/cols.
    int cols = (int)rotated.size() / a;
    int rows = a;
    std::vector<int> roundtrip(rotated.size());
    for (int j = 0; j < cols; j++) {
        for (int i = 0; i < rows; i++) {
            roundtrip[j * rows + i] = rotated[i * cols + j];
        }
    }
    CHECK_SEQ(roundtrip, orig, "rotate + transpose = identity");
}

void check_rotate_stride_one_innermost_noop() {
    // Stride-1 already innermost: no-op.
    MultiRamp A{0, {1, 10}, {3, 4}};
    auto before = expand(A);
    int a = A.rotate_stride_one_innermost();
    CHECK(a == 0, "no-op when stride-1 already innermost");
    CHECK_SEQ(expand(A), before, "unchanged");
}

// ---- is_multiramp round-trip --------------------------------------------

void check_roundtrip(const MultiRamp &mr, const char *msg) {
    Expr e = mr.to_expr();
    MultiRamp parsed;
    Scope<Expr> scope;
    if (e.type().is_vector()) {
        CHECK(is_multiramp(e, scope, &parsed), msg);
        if (parsed.dimensions() > 0 || mr.dimensions() > 0) {
            auto got = expand(parsed);
            auto want = expand(mr);
            CHECK_SEQ(got, want, msg);
        }
    }
}

void check_roundtrips() {
    check_roundtrip(MultiRamp{0, {1}, {4}}, "1D ramp roundtrip");
    check_roundtrip(MultiRamp{7, {1, 10}, {2, 3}}, "2D ramp roundtrip");
    check_roundtrip(MultiRamp{0, {1, 10, 100}, {2, 3, 2}}, "3D ramp roundtrip");
    check_roundtrip(MultiRamp{0, {0, 1}, {4, 3}}, "stride-zero dim roundtrip");
}

void check_reject_non_multiramp_sum() {
    // [0,1,2,100,101,102] + [0,2,100,102,200,202] = sum with shape conflict.
    Expr a_inner = Ramp::make(Expr(0), Expr(1), 3);
    Expr a = Ramp::make(a_inner, Broadcast::make(Expr(100), 3), 2);
    Expr b_inner = Ramp::make(Expr(0), Expr(2), 2);
    Expr b = Ramp::make(b_inner, Broadcast::make(Expr(100), 2), 3);
    Expr sum = Add::make(a, b);
    Scope<Expr> scope;
    MultiRamp m;
    CHECK(!is_multiramp(sum, scope, &m), "reject coprime-shape add");
}

}  // namespace

int main(int argc, char **argv) {
    check_add_refinable_shapes();
    check_add_same_shape();
    check_add_incompatible_shapes();
    check_add_cancels_to_zero();
    check_add_scaled_outer();

    check_div_pure_carry_const();
    check_div_symbolic_strides();
    check_div_merges_adjacent_pure_carry();
    check_div_with_split();
    check_div_split_with_symbolic_stride();
    check_div_rejects_non_multiramp();
    check_div_rejects_unaligned_base();
    check_div_rejects_symbolic_denominator();

    check_mod_basic();
    check_mod_with_split();
    check_mod_symbolic_strides();
    check_mod_rejects_non_multiramp();

    check_recognize_1d_ramp();
    check_recognize_nested_ramp();
    check_recognize_add();
    check_recognize_div_const();
    check_recognize_mod_const();
    check_recognize_div_symbolic_strides();
    check_reorder();
    check_shuffle_from_permuted_2d();
    check_shuffle_from_permuted_identity();
    check_shuffle_from_permuted_3d();
    check_shuffle_from_slice_2d();
    check_shuffle_from_slice_inner();
    check_shuffle_from_slice_3d();
    check_mul_basic();
    check_equality_same();
    check_equality_different();
    check_equality_scalar();
    check_alias_free_slice_all_unique();
    check_alias_free_slice_peels_zero_stride();
    check_alias_free_slice_degenerate();
    check_rotate_stride_one_innermost();
    check_rotate_stride_one_innermost_noop();
    check_roundtrips();
    check_reject_non_multiramp_sum();

    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}
