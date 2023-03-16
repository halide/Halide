#include "XtensaOptimize.h"

#include "AlignLoads.h"
#include "Bounds.h"
#include "CSE.h"
#include "ConciseCasts.h"
#include "Expr.h"
#include "ExprUsesVar.h"
#include "FindIntrinsics.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Lerp.h"
#include "LoopCarry.h"
#include "Simplify.h"
#include "Substitute.h"
#include <utility>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

using namespace Halide::ConciseCasts;

template<>
bool is_native_xtensa_vector<int8_t>(const Type &t, const Target &target) {
    int vector_size = target.natural_vector_size<int8_t>();
    return t.is_int() && (t.bits() == 8) && (t.lanes() == vector_size);
}

template<>
bool is_native_xtensa_vector<uint8_t>(const Type &t, const Target &target) {
    int vector_size = target.natural_vector_size<uint8_t>();
    return t.is_uint() && (t.bits() == 8) && (t.lanes() == vector_size);
}

template<>
bool is_native_xtensa_vector<int16_t>(const Type &t, const Target &target) {
    int vector_size = target.natural_vector_size<int16_t>();
    return t.is_int() && (t.bits() == 16) && (t.lanes() == vector_size);
}

template<>
bool is_native_xtensa_vector<uint16_t>(const Type &t, const Target &target) {
    int vector_size = target.natural_vector_size<uint16_t>();
    return t.is_uint() && (t.bits() == 16) && (t.lanes() == vector_size);
}

template<>
bool is_native_xtensa_vector<int32_t>(const Type &t, const Target &target) {
    int vector_size = target.natural_vector_size<int32_t>();
    return t.is_int() && (t.bits() == 32) && (t.lanes() == vector_size);
}

template<>
bool is_native_xtensa_vector<int64_t>(const Type &t, const Target &target) {
    // On Xtensa int64 vectors are *wide* vectors, so the number of lanes match
    // the number of lanes for 32-bit vectors.
    int vector_size = target.natural_vector_size<int32_t>();
    return t.is_int() && (t.bits() == 64) && (t.lanes() == vector_size);
}

template<>
bool is_native_xtensa_vector<uint32_t>(const Type &t, const Target &target) {
    int vector_size = target.natural_vector_size<uint32_t>();
    return t.is_uint() && (t.bits() == 32) && (t.lanes() == vector_size);
}

template<>
bool is_native_xtensa_vector<float16_t>(const Type &t, const Target &target) {
    int vector_size = target.natural_vector_size<float16_t>();
    return t.is_float() && (t.bits() == 16) && (t.lanes() == vector_size);
}

template<>
bool is_native_xtensa_vector<float>(const Type &t, const Target &target) {
    int vector_size = target.natural_vector_size<float>();
    return t.is_float() && (t.bits() == 32) && (t.lanes() == vector_size);
}

bool is_native_vector_type(const Type &t, const Target &target) {
    int native_lanes = target.natural_vector_size<uint8_t>();

    if (t.is_int_or_uint() && (t.lanes() == native_lanes) && (t.bits() == 8)) {
        return true;
    }

    if (t.is_int_or_uint() && (t.lanes() == native_lanes) && (t.bits() == 24)) {
        return true;
    }

    if (t.is_int_or_uint() && (t.lanes() == native_lanes / 2) && (t.bits() == 16)) {
        return true;
    }

    if (t.is_int_or_uint() && (t.lanes() == native_lanes / 2) && (t.bits() == 48)) {
        return true;
    }

    if (t.is_int_or_uint() && (t.lanes() == native_lanes / 4) && (t.bits() == 32)) {
        return true;
    }

    if (t.is_float() && (t.lanes() == native_lanes / 4) && (t.bits() == 32)) {
        return true;
    }

    return false;
}

bool is_double_native_vector_type(const Type &t, const Target &target) {
    int single_vector_bitwidth = 8 * target.natural_vector_size<uint8_t>();

    int double_vector_bitwidth = single_vector_bitwidth * 2;
    return (t.bits() % 8 == 0) && (double_vector_bitwidth % t.bits() == 0) && (double_vector_bitwidth / t.bits() == t.lanes());
}

Type get_native_xtensa_vector(const Type &t, const Target &target) {
    int vector_bitwidth = target.has_feature(Target::Feature::XtensaQ8) ? 1024 : 512;
    int wide_vector_bitwidth = target.has_feature(Target::Feature::XtensaQ8) ? 4096 : 1536;

    if (t.bits() == 64) {
        return t.with_lanes(vector_bitwidth / 32);
    }

    if (t.bits() == 24 || t.bits() == 48) {
        return t.with_lanes(wide_vector_bitwidth / t.bits());
    }
    return t.with_lanes(vector_bitwidth / t.bits());
}

std::string suffix_for_type(Type t) {
    if (t.is_bool()) {
        return "_u1";
    } else if (t.is_int() && (t.bits() == 8)) {
        return "_i8";
    } else if (t.is_uint() && (t.bits() == 8)) {
        return "_u8";
    } else if (t.is_int() && (t.bits() == 16)) {
        return "_i16";
    } else if (t.is_uint() && (t.bits() == 16)) {
        return "_u16";
    } else if (t.is_int() && (t.bits() == 32)) {
        return "_i32";
    } else if (t.is_uint() && (t.bits() == 32)) {
        return "_u32";
    } else if (t.is_float() && (t.bits() == 32)) {
        return "_f32";
    } else if (t.is_float() && (t.bits() == 16)) {
        return "_f16";
    }

    return "";
}

struct Pattern {
    enum Flags {
        InterleaveResult = 1 << 0,  // After evaluating the pattern, interleave native vectors of the result.
        SwapOps01 = 1 << 1,         // Swap operands 0 and 1 prior to substitution.
        SwapOps12 = 1 << 2,         // Swap operands 1 and 2 prior to substitution.
        ExactLog2Op1 = 1 << 3,      // Replace operand 1 with its log base 2, if the log base 2 is exact.
        ExactLog2Op2 = 1 << 4,      // Save as above, but for operand 2.

        BeginExactLog2Op = 1,  // BeginExactLog2Op and EndExactLog2Op ensure that we check only op1 and op2
        EndExactLog2Op = 3,    // for ExactLog2Op

        NarrowOp0 = 1 << 10,  // Replace operand 0 with its half-width equivalent.
        NarrowOp1 = 1 << 11,  // Same as above, but for operand 1.
        NarrowOp2 = 1 << 12,
        NarrowOp3 = 1 << 13,
        NarrowOp4 = 1 << 14,
        NarrowOps = NarrowOp0 | NarrowOp1 | NarrowOp2 | NarrowOp3 | NarrowOp4,

        NarrowUnsignedOp0 = 1 << 15,  // Similar to the above, but narrow to an unsigned half width type.
        NarrowUnsignedOp1 = 1 << 16,
        NarrowUnsignedOp2 = 1 << 17,
        NarrowUnsignedOp3 = 1 << 18,
        NarrowUnsignedOp4 = 1 << 19,

        NarrowUnsignedOps = NarrowUnsignedOp0 | NarrowUnsignedOp1 | NarrowUnsignedOp2 | NarrowUnsignedOp3 | NarrowUnsignedOp4,

        AccumulatorOutput24 = 1 << 20,
        AccumulatorOutput48 = 1 << 21,
        AccumulatorOutput64 = 1 << 22,

        PassOnlyOp0 = 1 << 23,
        PassOnlyOp1 = 1 << 24,
        PassOnlyOp2 = 1 << 25,
        PassOnlyOp3 = 1 << 26,

        PassOps = PassOnlyOp0 | PassOnlyOp1 | PassOnlyOp2 | PassOnlyOp3,
        BeginPassOnlyOp = 0,  // BeginPassOnlyOp and EndPassOnlyOp ensure that we check only
        EndPassOnlyOp = 4,    // PassOps[0|1|2|3].

        SameOp01 = 1 << 27,
        SameOp12 = 1 << 28,
    };

    std::string intrin;  // Name of the intrinsic
    Expr pattern;        // The pattern to match against
    int flags;

    Pattern() = default;
    Pattern(const std::string &intrin, Expr p, int flags = 0)
        : intrin(intrin), pattern(std::move(p)), flags(flags) {
    }
};

Expr wild_u8 = Variable::make(UInt(8), "*");
Expr wild_u16 = Variable::make(UInt(16), "*");
Expr wild_u32 = Variable::make(UInt(32), "*");
Expr wild_u64 = Variable::make(UInt(64), "*");
Expr wild_i8 = Variable::make(Int(8), "*");
Expr wild_i16 = Variable::make(Int(16), "*");
Expr wild_i24 = Variable::make(Int(24), "*");
Expr wild_i32 = Variable::make(Int(32), "*");
Expr wild_i64 = Variable::make(Int(64), "*");
Expr wild_f32 = Variable::make(Float(32), "*");

Expr wild_u1x = Variable::make(Type(Type::UInt, 1, 0), "*");
Expr wild_u8x = Variable::make(Type(Type::UInt, 8, 0), "*");
Expr wild_u16x = Variable::make(Type(Type::UInt, 16, 0), "*");
Expr wild_u32x = Variable::make(Type(Type::UInt, 32, 0), "*");
Expr wild_u64x = Variable::make(Type(Type::UInt, 64, 0), "*");
Expr wild_i8x = Variable::make(Type(Type::Int, 8, 0), "*");
Expr wild_i8x4 = Variable::make(Type(Type::Int, 8, 4), "*");
Expr wild_i8x64 = Variable::make(Type(Type::Int, 8, 64), "*");
Expr wild_i8x256 = Variable::make(Type(Type::Int, 8, 256), "*");
Expr wild_u8x4 = Variable::make(Type(Type::UInt, 8, 4), "*");
Expr wild_u8x64 = Variable::make(Type(Type::UInt, 8, 64), "*");
Expr wild_u8x256 = Variable::make(Type(Type::UInt, 8, 256), "*");

Expr wild_i16x = Variable::make(Type(Type::Int, 16, 0), "*");
Expr wild_i24x = Variable::make(Type(Type::Int, 24, 0), "*");
Expr wild_i24x64 = Variable::make(Type(Type::Int, 24, 64), "*");
Expr wild_i24x128 = Variable::make(Type(Type::Int, 24, 128), "*");
Expr wild_i24x256 = Variable::make(Type(Type::Int, 24, 256), "*");
Expr wild_i32x = Variable::make(Type(Type::Int, 32, 0), "*");
Expr wild_i48x = Variable::make(Type(Type::Int, 48, 0), "*");
Expr wild_i64x = Variable::make(Type(Type::Int, 64, 0), "*");
Expr wild_f32x = Variable::make(Type(Type::Float, 32, 0), "*");

inline Expr i24(Expr e) {
    Type t = Int(24, e.type().lanes());
    return cast(t, std::move(e));
}

inline Expr i48(Expr e) {
    Type t = Int(48, e.type().lanes());
    return cast(t, std::move(e));
}

// Broadcast to an unknown number of lanes, for making patterns.
Expr bc(Expr x, int lanes = 0) {
    return Broadcast::make(std::move(x), lanes);
}

Expr ramp(Expr base, Expr stride, int lanes = 0) {
    return Ramp::make(std::move(base), std::move(stride), lanes);
}

Expr vector_reduce(VectorReduce::Operator op, Expr x) {
    return VectorReduce::make(op, std::move(x), 0);
}

Expr call(const string &name, const Expr &return_type, const vector<Expr> &args) {
    return Call::make(return_type.type(), name, args, Call::PureExtern);
}

Expr concat(const vector<Expr> &x) {
    return Shuffle::make_concat(x);
}

Expr repeat_each_element(Expr x, int times) {
    vector<int> indices;
    for (int ix = 0; ix < x.type().lanes(); ix++) {
        for (int iy = 0; iy < times; iy++) {
            indices.push_back(ix);
        }
    }
    return Shuffle::make({std::move(x)}, indices);
}

Expr slice(Expr x, int begin, int stride, int size) {
    return Shuffle::make_slice(std::move(x), begin, stride, size);
}

Expr load(const Type &type, const string &name, Expr index, ModulusRemainder alignment) {
    return Load::make(type, name, std::move(index), Buffer<>(), Parameter(), const_true(), alignment);
}

// Check if the matches satisfy the given pattern flags, and mutate the matches
// as specified by the flags.
bool process_match_flags(vector<Expr> &matches, int flags) {
    // The Pattern::Narrow*Op* flags are ordered such that the operand
    // corresponds to the bit (with operand 0 corresponding to the least
    // significant bit), so we can check for them all in a loop.
    for (size_t i = 0; i < matches.size(); i++) {
        Type t = matches[i].type();
        Type target_t = t.with_bits(t.bits() / 2);
        if (flags & (Pattern::NarrowOp0 << i)) {
            matches[i] = lossless_cast(target_t, matches[i]);
        } else if (flags & (Pattern::NarrowUnsignedOp0 << i)) {
            matches[i] = lossless_cast(target_t.with_code(Type::UInt), matches[i]);
        }
        if (!matches[i].defined()) {
            return false;
        }
    }

    for (size_t i = Pattern::BeginExactLog2Op; i < Pattern::EndExactLog2Op; i++) {
        // This flag is mainly to capture shifts. When the operand of a div or
        // mul is a power of 2, we can use a shift instead.
        if (flags & (Pattern::ExactLog2Op1 << (i - Pattern::BeginExactLog2Op))) {
            int pow;
            if (is_const_power_of_two_integer(matches[i], &pow)) {
                matches[i] = cast(matches[i].type().with_lanes(1), pow);
            } else {
                return false;
            }
        }
    }

    if (flags & Pattern::PassOps) {
        vector<Expr> new_matches;
        for (size_t i = Pattern::BeginPassOnlyOp; i < Pattern::EndPassOnlyOp; i++) {
            if (flags & (Pattern::PassOnlyOp0 << (i - Pattern::BeginPassOnlyOp))) {
                new_matches.push_back(matches[i]);
            }
        }
        matches.swap(new_matches);
    }

    if (flags & Pattern::SwapOps01) {
        internal_assert(matches.size() >= 2);
        std::swap(matches[0], matches[1]);
    }
    if (flags & Pattern::SwapOps12) {
        internal_assert(matches.size() >= 3);
        std::swap(matches[1], matches[2]);
    }

    if (flags & Pattern::SameOp01) {
        internal_assert(matches.size() == 2);
        if (!graph_equal(matches[0], matches[1])) {
            return false;
        }
        matches = {matches[0]};
    }

    if (flags & Pattern::SameOp12) {
        internal_assert(matches.size() == 3);
        if (!graph_equal(matches[1], matches[2])) {
            return false;
        }
        matches = {matches[0], matches[1]};
    }

    return true;
}

// Replace an expression with the one specified by a pattern.
Expr replace_pattern(Expr x, const vector<Expr> &matches, const Pattern &p) {
    x = Call::make(x.type(), p.intrin, matches, Call::PureExtern);
    return x;
}
// Attempt to apply one of the patterns to x. If a match is
// successful, the expression is replaced with a call using the
// matched operands. Prior to substitution, the matches are mutated
// with op_mutator.
Expr apply_patterns(Expr x, const vector<Pattern> &patterns, IRMutator *op_mutator) {
    debug(3) << "apply_patterns " << x << "\n";
    vector<Expr> matches;
    for (const Pattern &p : patterns) {
        if (expr_match(p.pattern, x, matches)) {
            debug(3) << "matched " << p.pattern << "\n";
            debug(3) << "to " << x << "\n";
            debug(3) << "matches:\n";
            for (const Expr &i : matches) {
                debug(3) << i << "\n";
            }

            if (!process_match_flags(matches, p.flags)) {
                continue;
            }

            // Mutate the operands with the given mutator.
            for (Expr &op : matches) {
                op = op_mutator->mutate(op);
            }

            Type old_type = x.type();
            if (p.flags & Pattern::AccumulatorOutput24) {
                x = cast(Type(Type::Int, 24, x.type().lanes()), x);
            } else if (p.flags & Pattern::AccumulatorOutput48) {
                x = cast(Type(Type::Int, 48, x.type().lanes()), x);
            } else if (p.flags & Pattern::AccumulatorOutput64) {
                x = cast(Type(Type::Int, 64, x.type().lanes()), x);
            }
            x = replace_pattern(x, matches, p);
            if ((p.flags & Pattern::AccumulatorOutput24) || (p.flags & Pattern::AccumulatorOutput48) || (p.flags & Pattern::AccumulatorOutput64)) {
                x = cast(old_type, x);
            }

            debug(3) << "rewrote to: " << x << "\n";
            return x;
        }
    }
    return x;
}

template<typename T>
Expr apply_commutative_patterns(const T *op, const vector<Pattern> &patterns, IRMutator *mutator) {
    Expr ret = apply_patterns(op, patterns, mutator);
    if (!ret.same_as(op)) {
        return ret;
    }

    // Try commuting the op
    Expr commuted = T::make(op->b, op->a);
    ret = apply_patterns(commuted, patterns, mutator);
    if (!ret.same_as(commuted)) {
        return ret;
    }

    return op;
}

/** A helper for block_to_vector below. */
void block_to_vector(const Stmt &s, vector<Stmt> &v) {
    const Block *b = s.as<Block>();
    if (!b) {
        v.push_back(s);
    } else {
        block_to_vector(b->first, v);
        block_to_vector(b->rest, v);
    }
}

/** Unpack a block into its component Stmts. */
vector<Stmt> block_to_vector(const Stmt &s) {
    vector<Stmt> result;
    block_to_vector(s, result);
    return result;
}

class DualQuadMulMutator : public IRGraphMutator {
private:
    using IRGraphMutator::visit;

    Expr visit(const Shuffle *op) override {

        // Merge concat extract i32 calls into one dual call
        if (op->is_concat() && op->vectors.size() == 2) {
            const Call *call0 = op->vectors[0].as<Call>();
            const Call *call1 = op->vectors[1].as<Call>();
            if (call0 && call0->name == "halide_xtensa_extract_u32" &&
                call1 && call1->name == "halide_xtensa_extract_u32") {
                vector<Expr> dual_args = {
                    call1->args[0],  // vector1
                    call0->args[0],  // vector0
                    call1->args[1],  // index1
                    call0->args[1]   // index0
                };
                return Call::make(Int(8, 8), "halide_xtensa_dual_extract_i32",
                                  dual_args, Call::PureExtern);
            }
        }

        return IRGraphMutator::visit(op);
    };

    Stmt visit(const Block *op) override {
        vector<Stmt> new_stmts;

        vector<Stmt> stmts = block_to_vector(op);
        int quad_mul_expr_count = 0;
        // Check if all statements in the block are stores of quad-muls.
        for (auto &stmt : stmts) {
            // quad_mul is a call contained in store
            const Store *store1 = stmt.as<Store>();
            const Call *call1 = store1 ? store1->value.as<Call>() : nullptr;
            if (!call1 || call1->name != "halide_xtensa_widen_quad_mul_add_u24") {
                break;
            }
            quad_mul_expr_count++;
        }

        if (quad_mul_expr_count > 1) {
            // Try to find pairs of quad-muls which have matching second argument.
            // Track which statements have been used so far.
            vector<bool> used(stmts.size(), false);
            for (int first = 0; first < quad_mul_expr_count; first++) {
                for (int second = first + 1; second < quad_mul_expr_count; second++) {
                    if (used[first] || used[second]) {
                        continue;
                    }

                    const Store *store1 = stmts[first].as<Store>();
                    const Call *call1 = store1->value.as<Call>();

                    const Store *store2 = stmts[second].as<Store>();
                    const Call *call2 = store2->value.as<Call>();

                    // Check if two quad-muls have the same operand.
                    if ((call1->args.size() != 3) || (call2->args.size() != 3) || !equal(call1->args[1], call2->args[1])) {
                        continue;
                    }

                    used[first] = true;
                    used[second] = true;

                    // Update stores to take from dual call result
                    std::string dual_name = unique_name("_");
                    Expr dual_24x64 = Variable::make(Type(Type::Int, 24, call1->type.lanes() + call2->type.lanes()),
                                                     dual_name);
                    Expr slice0 = Shuffle::make_slice(dual_24x64, 0, 1, call1->type.lanes());
                    Expr slice1 = Shuffle::make_slice(dual_24x64, call1->type.lanes(), 1, call2->type.lanes());
                    Stmt new_store0 = Store::make(store1->name, slice0, store1->index,
                                                  store1->param, store1->predicate, store1->alignment);
                    Stmt new_store1 = Store::make(store2->name, slice1, store2->index,
                                                  store2->param, store2->predicate, store2->alignment);
                    Stmt stores = Block::make(new_store0, new_store1);

                    // Collect inputs for dual call
                    std::vector<Expr> dual_qm_args = {
                        concat({call1->args[0], call2->args[0]}),
                        call1->args[1],
                        // two of uint8x4_t multipliers.
                        concat({call1->args[2], call2->args[2]})};

                    // Insert LetStmt with dual call with store scope
                    new_stmts.push_back(
                        LetStmt::make(
                            dual_name,
                            call("halide_xtensa_dual_widen_quad_mul_add_u24", dual_24x64, dual_qm_args),
                            stores));
                }
            }

            // In the case we haven't used all statements (for example, couldn't find a pair)
            // just add remaining quad muls to the list of statements.
            for (int ix = 0; ix < (int)stmts.size(); ix++) {
                if (!used[ix]) {
                    new_stmts.push_back(stmts[ix]);
                }
            }
        } else {
            // Not all statements are stores of quad-muls, so just use the old ones.
            new_stmts = stmts;
        }

        // Recursively mutate and check size to see if there is any merge
        for (Stmt &i : new_stmts) {
            i = mutate(i);
        }
        bool unchanged = new_stmts.size() == stmts.size();
        if (unchanged) {
            for (int i = 0; i < (int)new_stmts.size(); ++i) {
                unchanged = unchanged && new_stmts[i].same_as(stmts[i]);
            }
        }

        if (unchanged) {
            return op;
        } else {
            return Block::make(new_stmts);
        }
    }
};

class MatchXtensaPatterns : public IRGraphMutator {
private:
    using IRGraphMutator::visit;

    const Target target;

    static Expr halide_xtensa_widen_mul_u24(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i24x.type(), "halide_xtensa_widen_mul_u24", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_widen_mul_by_diff_u24(Expr v0, Expr v1, Expr v2) {
        Expr call = Call::make(wild_i24x.type(), "halide_xtensa_widen_mul_by_diff_u24", {std::move(v0), std::move(v1), std::move(v2)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_widen_mul_i48(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_widen_mul_i48", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_widen_mul_add_i48(Expr v0, Expr v1, Expr v2) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_widen_mul_add_i48", {std::move(v0), std::move(v1), std::move(v2)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_widen_add_i48(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_widen_add_i48", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_widen_add_u48(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_widen_add_u48", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_widen_pair_mul_i48(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_widen_pair_mul_i48",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)},
                               Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_widen_pair_mul_add_i48(Expr w, Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_widen_pair_mul_add_i48",
                               {std::move(w), std::move(v0), std::move(v1), std::move(v2), std::move(v3)},
                               Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_slice_to_native_i32(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_i32x.type(), "halide_xtensa_slice_to_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_slice_to_native_u32(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_u32x.type(), "halide_xtensa_slice_to_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_slice_to_native_i16(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_i16x.type(), "halide_xtensa_slice_to_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_slice_to_native_u16(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_u16x.type(), "halide_xtensa_slice_to_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_i16(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i16x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_u16(Expr v0, Expr v1) {
        Expr call = Call::make(wild_u16x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_i24(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i24x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_i32(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i32x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_i32(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_i32x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)},
                               Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_u32(Expr v0, Expr v1) {
        Expr call = Call::make(wild_u32x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_u1(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_u1x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_i48(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    Expr visit(const Add *op) override {
        if (op->type.is_vector()) {
            static const std::vector<Pattern> adds = {
                // Predicated addition
                // NOTE(vksnk): patterns below are for predicated instructions and look like they may
                // be more efficient, but they are not according to simulator. We will need to check with
                // Cadence about this.
                // {"halide_xtensa_pred_add_i8", wild_i8x + select(wild_u1x, wild_i8x, wild_i8x)},
                // {"halide_xtensa_pred_add_i16", wild_i16x + select(wild_u1x, wild_i16x, wild_i16x)},
                // {"halide_xtensa_pred_add_i32", wild_i32x + select(wild_u1x, wild_i32x, wild_i32x)},

                {"halide_xtensa_qqqq", slice(wild_i24x256, 0, 1, 128) + slice(wild_i24x256, 128, 1, 128), Pattern::SameOp01},
                {"halide_xtensa_yyyy", (call("halide_xtensa_xxxx", wild_i24x64, {wild_i24x64, wild_i24x128}) + slice(wild_i24x128, 64, 1, 64)), Pattern::SameOp12},
                {"halide_xtensa_xxxx", (wild_i24x64 + slice(wild_i24x128, 0, 1, 64))},

                {"halide_xtensa_widen_quad_add_i48", widening_add(wild_i16x, wild_i16x) + widening_add(wild_i16x, wild_i16x), Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_quad_add_i48", i32(halide_xtensa_widen_add_i48(wild_i16x, wild_i16x)) + i32(halide_xtensa_widen_add_i48(wild_i16x, wild_i16x)), Pattern::AccumulatorOutput48},

                {"halide_xtensa_widen_pair_mul_i48", widening_mul(wild_i16x, wild_i16x) + widening_mul(wild_i16x, wild_i16x), Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_pair_mul_u48", widening_mul(wild_u16x, wild_u16x) + widening_mul(wild_u16x, wild_u16x), Pattern::AccumulatorOutput48},

                {"halide_xtensa_widen_pair_mul_i48", i48(wild_i16x) * i48(wild_i16x) + i48(wild_i16x) * i48(wild_i16x)},
                {"halide_xtensa_widen_pair_mul_u48", i48(wild_u16x) * i48(wild_u16x) + i48(wild_u16x) * i48(wild_u16x)},

                {"halide_xtensa_widen_pair_mul_i24", i24(wild_i8x) * i24(wild_i8x) + i24(wild_i8x) * i24(wild_i8x)},
                {"halide_xtensa_widen_pair_mul_u24", i24(wild_u8x) * i24(wild_u8x) + i24(wild_u8x) * i24(wild_u8x)},

                // Multiply-add to accumulator type.
                {"halide_xtensa_widen_pair_mul_add_i48", i32(halide_xtensa_widen_mul_add_i48(wild_i48x, wild_i16x, wild_i16x)) + i32(halide_xtensa_widen_mul_i48(wild_i16x, wild_i16x)), Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_pair_mul_add_i48", halide_xtensa_widen_mul_add_i48(wild_i48x, wild_i16x, wild_i16x) + halide_xtensa_widen_mul_i48(wild_i16x, wild_i16x)},

                {"halide_xtensa_widen_mul_add_i48", i32(wild_i48x) + i32(halide_xtensa_widen_mul_i48(wild_i16x, wild_i16x)), Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_mul_add_i48", wild_i48x + halide_xtensa_widen_mul_i48(wild_i16x, wild_i16x)},

                {"halide_xtensa_widen_mul_add_u24", wild_i24x + halide_xtensa_widen_mul_u24(wild_u8x, wild_u8x)},
                {"halide_xtensa_widen_mul_add_by_diff_u24", wild_i24x + halide_xtensa_widen_mul_by_diff_u24(wild_u8x, wild_u8, wild_u8x)},

                {"halide_xtensa_widen_mul_add_i24",
                 wild_i24x + call("halide_xtensa_widen_mul_i24", wild_i24x, {wild_i8x, wild_i8x})},

                {"halide_xtensa_widen_quad_mul_add_i24",
                 wild_i24x + call("halide_xtensa_widen_quad_mul_i24", wild_i24x, {wild_i8x, wild_i8x, wild_i8x, wild_i8x, wild_i8x})},

                // Add to accumulator type.
                // Paired add.
                {"halide_xtensa_widen_pair_add_i48", i32(halide_xtensa_widen_add_i48(wild_i48x, wild_i16x)) + wild_i16x, Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_pair_add_i48", i32(halide_xtensa_widen_add_i48(wild_i48x, wild_i16x)) + wild_i32x, Pattern::AccumulatorOutput48 | Pattern::NarrowOp2},
                {"halide_xtensa_widen_pair_add_u48", u32(halide_xtensa_widen_add_u48(wild_i48x, wild_u16x)) + wild_u16x, Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_pair_add_u48", u32(halide_xtensa_widen_add_u48(wild_i48x, wild_u16x)) + wild_u32x, Pattern::AccumulatorOutput48 | Pattern::NarrowUnsignedOp2},
                // Single add.
                {"halide_xtensa_widen_add_i48", i32(wild_i48x) + wild_i16x, Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_add_i48", i32(wild_i48x) + wild_i32x, Pattern::AccumulatorOutput48 | Pattern::NarrowOp1},
                {"halide_xtensa_widen_add_u48", u32(wild_i48x) + wild_u16x, Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_add_u48", u32(wild_i48x) + wild_u32x, Pattern::AccumulatorOutput48 | Pattern::NarrowUnsignedOp1},

                {"halide_xtensa_widen_add_i24", i16(wild_i24x) + wild_i8x, Pattern::AccumulatorOutput24},
                {"halide_xtensa_widen_add_i24", i16(wild_i24x) + wild_i16x, Pattern::AccumulatorOutput24 | Pattern::NarrowOp1},

                {"halide_xtensa_widen_mul_add_i64", widening_mul(wild_i32x, wild_i32x) + bc(wild_i64), Pattern::NarrowOp2 | Pattern::AccumulatorOutput64},
                {"halide_xtensa_widen_mul_add_i64", widening_mul(wild_i32x, wild_i32x) + wild_i64x, Pattern::NarrowOp2 | Pattern::AccumulatorOutput64},
                {"halide_xtensa_widen_mul_add_i64", i32(wild_i64x) + i32(call("halide_xtensa_mul_i32", wild_i64x, {wild_i32x, wild_i32x})), Pattern::AccumulatorOutput64},
            };

            Expr new_expr = apply_commutative_patterns(op, adds, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Sub *op) override {
        if (op->type.is_vector()) {
            static const std::vector<Pattern> subs = {
                // Predicated sub.
                // NOTE(vksnk): patterns below are for predicated instructions and look like they may
                // be more efficient, but they are not according to simulator. We will need to check with
                // Cadence about this.
                // {"halide_xtensa_pred_sub_i8", wild_i8x - select(wild_u1x, wild_i8x, wild_i8x)},
                // {"halide_xtensa_pred_sub_i16", wild_i16x - select(wild_u1x, wild_i16x, wild_i16x)},
                // {"halide_xtensa_pred_sub_i32", wild_i32x - select(wild_u1x, wild_i32x, wild_i32x)},
                {"halide_xtensa_widen_mul_sub_u24", wild_i24x - halide_xtensa_widen_mul_u24(wild_u8x, wild_u8x)},
            };

            Expr new_expr = apply_patterns(op, subs, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Mul *op) override {
        if (op->type.is_vector()) {
            static const std::vector<Pattern> scalar_muls = {};

            static const std::vector<Pattern> muls = {
                {"halide_xtensa_widen_mul_i24", i24(wild_i8x) * bc(i24(wild_i8))},
                {"halide_xtensa_widen_mul_u24", i24(wild_u8x) * bc(i24(wild_u8))},

                {"halide_xtensa_widen_mul_i24", i24(wild_i8x) * i24(wild_i8x)},
                {"halide_xtensa_widen_mul_u24", i24(wild_u8x) * i24(wild_u8x)},

                {"halide_xtensa_widen_mul_by_diff_u24", (i24(wild_u8x) - bc(i24(wild_u8))) * bc(i24(wild_u8))},
                {"halide_xtensa_widen_mul_by_diff_u24", (i24(wild_u8x) - bc(i24(wild_u8))) * i24(wild_u8x)},

                {"halide_xtensa_widen_mul_i48", i48(wild_i16x) * i48(wild_i16x)},

                {"halide_xtensa_mul_i32", wild_i32x * wild_i32x, Pattern::AccumulatorOutput64},

                {"halide_xtensa_widen_zzzzz", i24(concat({wild_i8x64, wild_i8x64, wild_i8x64, wild_i8x64})) * i24(repeat_each_element(wild_i8x4, 64))},
                {"halide_xtensa_widen_zzzzz", i24(wild_i8x256) * i24(repeat_each_element(wild_i8x4, 64))},
                {"halide_xtensa_widen_zzzzz", i24(wild_u8x256) * bc(i24(wild_u8), 256)},
                {"halide_xtensa_widen_zzzzz", i24(concat({wild_u8x64, wild_u8x64, wild_u8x64, wild_u8x64})) * i24(repeat_each_element(wild_u8x4, 64))},
                {"halide_xtensa_widen_zzzzz", i24(wild_u8x256) * i24(repeat_each_element(wild_u8x4, 64))},

                // Widening multiplication
                // NOTE(vksnk): looked like a good idea, but seems to be slower. Need to double-check.
                // {"halide_xtensa_widen_sqr_i48", wild_i32x * wild_i32x, Pattern::SameOp01 | Pattern::NarrowOps | Pattern::AccumulatorOutput48},
            };

            Expr new_expr = apply_commutative_patterns(op, scalar_muls, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }

            new_expr = apply_commutative_patterns(op, muls, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Div *op) override {
        if (op->type.is_vector()) {
            Expr div = op;
            static const std::vector<Pattern> divs = {
                // TODO(vksnk): Before enabling it add a check for ExactLogOp
                // {"halide_xtensa_div_i32_i16", wild_i32x / wild_i32x, Pattern::NarrowOp1}
                {"halide_xtensa_narrow_i48_with_shift_i32", i32(wild_i48x) / wild_i32, Pattern::ExactLog2Op1},
                {"halide_xtensa_narrow_i48_with_shift_u32", u32(wild_i48x) / wild_u32, Pattern::ExactLog2Op1},
            };

            Expr new_expr = apply_patterns(div, divs, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Max *op) override {
        if (op->type.is_vector()) {
            static const std::vector<Pattern> maxes = {
                // NOTE(vksnk): patterns below are for predicated instructions and look like they may
                // be more efficient, but they are not according to simulator. We will need to check with
                // Cadence about this.
                // {"halide_xtensa_pred_max_i16", max(wild_i16x, select(wild_u1x, wild_i16x, wild_i16x))}
            };

            Expr new_expr = apply_commutative_patterns(op, maxes, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Min *op) override {
        if (op->type.is_vector()) {
            static const std::vector<Pattern> maxes = {
                // NOTE(vksnk): patterns below are for predicated instructions and look like they may
                // be more efficient, but they are not according to simulator. We will need to check with
                // Cadence about this.
                // {"halide_xtensa_pred_min_i16", max(wild_i16x, select(wild_u1x, wild_i16x, wild_i16x))}
            };

            Expr new_expr = apply_commutative_patterns(op, maxes, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Cast *op) override {
        // TODO(vksnk): disable widening_load until correctness issue is fixed.
        // // Try to look for widening loads.
        // if (const Load *load = op->value.as<Load>()) {
        //     Expr dense_ramp_base = strided_ramp_base(load->index, 1);
        //     if (dense_ramp_base.defined() && is_const_one(load->predicate) && (op->type.is_int_or_uint()) && ((op->type.bits() == 16) || (op->type.bits() == 32)) && (load->type.is_int_or_uint()) && (2 * load->type.bits() == op->type.bits())) {
        //         // The third argument is just to pass the type of load.
        //         return Call::make(op->type, "halide_xtensa_widening_load", {Variable::make(type_of<void *>(), load->name), dense_ramp_base, make_one(load->type.element_of())}, Call::PureExtern);
        //     }
        // }

        // if (const Shuffle *concat = op->value.as<Shuffle>()) {
        //     if (concat->is_concat()) {
        //         std::vector<Expr> widened_loads;
        //         for (const Expr &v : concat->vectors) {
        //             if (const Load *load = v.as<Load>()) {
        //                 Expr dense_ramp_base = strided_ramp_base(load->index, 1);
        //                 if (dense_ramp_base.defined() && is_const_one(load->predicate) && (op->type.is_int_or_uint()) && ((op->type.bits() == 16) || (op->type.bits() == 32)) && (load->type.is_int_or_uint()) && (2 * load->type.bits() == op->type.bits())) {
        //                     // The third argument is just to pass the type of load.
        //                     widened_loads.push_back(Call::make(op->type.with_lanes(v.type().lanes()), "halide_xtensa_widening_load", {Variable::make(type_of<void *>(), load->name), dense_ramp_base, make_one(load->type.element_of())}, Call::PureExtern));
        //                 }
        //             }
        //         }

        //         if (widened_loads.size() == concat->vectors.size()) {
        //             return Shuffle::make_concat(widened_loads);
        //         }
        //     }
        // }

        static const std::vector<Pattern> casts = {
            // Narrowing multiply with shift.
            // {"halide_xtensa_sat_mul_with_shift_i32", i32(wild_i64x * wild_i64x / wild_i64), Pattern::NarrowOp0 | Pattern::NarrowUnsignedOp1 | Pattern::ExactLog2Op2},

            // Casts from bool.
            {"halide_xtensa_convert_u1_to_i16", i16(i8(wild_u1x))},

            // Narrowing with shifting.
            {"halide_xtensa_narrow_i48_with_shift_i16", i16(i32(wild_i48x) >> wild_i32)},
            {"halide_xtensa_narrow_i48_with_shift_i16", i16(i32(wild_i48x) / wild_i32), Pattern::ExactLog2Op1},
            {"halide_xtensa_narrow_i48_with_shift_u16", u16(u32(wild_i48x) >> wild_u32)},
            {"halide_xtensa_narrow_i48_with_shift_u16", u16(u32(wild_i48x) / wild_u32), Pattern::ExactLog2Op1},

            {"halide_xtensa_narrow_i48_with_shift_i16", i16(wild_i48x >> wild_i32)},
            {"halide_xtensa_narrow_i48_with_shift_i16", i16(wild_i48x / wild_i32), Pattern::ExactLog2Op1},
            {"halide_xtensa_narrow_i48_with_shift_u16", u16(wild_i48x >> wild_u32)},
            {"halide_xtensa_narrow_i48_with_shift_u16", u16(wild_i48x / wild_u32), Pattern::ExactLog2Op1},

            {"halide_xtensa_narrow_i48_with_rounding_shift_i16", i16(rounding_shift_right(i32(wild_i48x), wild_i32))},
            {"halide_xtensa_narrow_i48_with_rounding_shift_u16", u16(rounding_shift_right(u32(wild_i48x), wild_u32))},

            {"halide_xtensa_narrow_with_shift_i16", i16(wild_i32x >> wild_i32)},
            {"halide_xtensa_narrow_with_shift_i16", i16(wild_i32x / wild_i32), Pattern::ExactLog2Op1},
            {"halide_xtensa_narrow_with_shift_u16", u16(wild_i32x >> wild_i32)},
            {"halide_xtensa_narrow_with_shift_u16", u16(wild_i32x / wild_i32), Pattern::ExactLog2Op1},

            {"halide_xtensa_narrow_with_rounding_shift_i8", i8(rounding_shift_right(wild_i16x, bc(wild_u16)))},
            {"halide_xtensa_narrow_with_rounding_shift_u8", u8(rounding_shift_right(wild_i16x, bc(wild_u16)))},
            {"halide_xtensa_narrow_with_rounding_shift_i16", i16(rounding_shift_right(wild_i32x, bc(wild_u32)))},

            // Looks like there is no such instruction.
            // {"halide_xtensa_sat_narrow_with_rounding_shift_u16", u16_sat(rounding_shift_right(wild_i32x, wild_u32))},

            {"halide_xtensa_narrow_i24_with_shift_i16", i16(wild_i24x >> wild_i24)},
            {"halide_xtensa_narrow_i24_with_shift_i16", i16(wild_i24x / wild_i24), Pattern::ExactLog2Op1},

            {"halide_xtensa_narrow_i24_with_shift_i8", i8(wild_i24x >> wild_i24)},
            {"halide_xtensa_narrow_i24_with_shift_i8", i8(wild_i24x / wild_i24), Pattern::ExactLog2Op1},
            {"halide_xtensa_narrow_i24_with_shift_u8", u8(wild_i24x >> wild_i24)},
            {"halide_xtensa_narrow_i24_with_shift_u8", u8(wild_i24x / wild_i24), Pattern::ExactLog2Op1},

            {"halide_xtensa_narrow_high_i32", i32(wild_i64x >> 32)},
            {"halide_xtensa_narrow_high_i32", i32(wild_i64x / IntImm::make(Int(64), 4294967296ll))},

            {"halide_xtensa_narrow_shift_i32", i32(wild_i64x >> bc(wild_i64))},
            {"halide_xtensa_narrow_shift_i32", i32(wild_i64x / bc(wild_i64)), Pattern::ExactLog2Op1},
            {"halide_xtensa_narrow_shift_i32", i32(wild_i64x >> bc(wild_u64))},
            {"halide_xtensa_narrow_shift_i32", i32(wild_i64x / bc(wild_u64)), Pattern::ExactLog2Op1},

            // Concat and cast.
            {"halide_xtensa_convert_concat_i16_to_i8", i8(halide_xtensa_concat_from_native_i16(wild_i16x, wild_i16x))},
            {"halide_xtensa_convert_concat_i16_to_u8", u8(halide_xtensa_concat_from_native_i16(wild_i16x, wild_i16x))},
            {"halide_xtensa_convert_concat_u16_to_i8", i8(halide_xtensa_concat_from_native_u16(wild_u16x, wild_u16x))},
            {"halide_xtensa_convert_concat_u16_to_u8", u8(halide_xtensa_concat_from_native_u16(wild_u16x, wild_u16x))},
            {"halide_xtensa_convert_concat_i32_to_i16", i16(halide_xtensa_concat_from_native_i32(wild_i32x, wild_i32x))},
            {"halide_xtensa_convert_concat_i32_to_u16", u16(halide_xtensa_concat_from_native_i32(wild_i32x, wild_i32x))},
            {"halide_xtensa_convert_concat_u32_to_i16", i16(halide_xtensa_concat_from_native_u32(wild_u32x, wild_u32x))},
            {"halide_xtensa_convert_concat_u32_to_u16", u16(halide_xtensa_concat_from_native_u32(wild_u32x, wild_u32x))},

            // NOTE(vksnk): looked like a good idea, but seems to be slower. Need to double-check.
            // {"halide_xtensa_narrow_clz_i16", i16(count_leading_zeros(wild_u32x))},
            // {"halide_xtensa_narrow_clz_i16", i16(count_leading_zeros(wild_i32x))},
        };
        if (op->type.is_vector()) {
            Expr cast = op;

            std::vector<Expr> matches;

            Expr new_expr = apply_patterns(cast, casts, this);
            if (!new_expr.same_as(cast)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Shuffle *op) override {
        if (op->is_slice() && (op->slice_stride() == 1) && (op->slice_begin() % 4 == 0) && op->type.is_int_or_uint() && (op->type.bits() == 8) && (op->type.lanes() == 4)) {

            return Call::make(op->type, std::string("halide_xtensa_extract_") + (op->type.is_int() ? "i32" : "u32"),
                              {mutate(op->vectors[0]), op->slice_begin() / 4}, Call::PureExtern);
        } else if (op->type.is_int_or_uint() && (op->type.bits() == 8) && (op->type.lanes() == 64)) {
            if ((op->vectors.size() == 1) && (op->vectors[0].type().lanes() == 192)) {
                bool is_extract_0_of_3 = true;
                for (int ix = 0; ix < (int)op->indices.size(); ix++) {
                    is_extract_0_of_3 = is_extract_0_of_3 && (op->indices[ix] == 3 * ix);
                }

                if (is_extract_0_of_3) {
                    Expr op_vector = mutate(op->vectors[0]);
                    vector<Expr> args = {op_vector};
                    const Shuffle *maybe_shuffle = op_vector.as<Shuffle>();
                    if (maybe_shuffle && maybe_shuffle->is_concat()) {
                        args = maybe_shuffle->vectors;
                    }
                    if (op->type.is_int()) {
                        return Call::make(op->type, "halide_xtensa_extract_0_of_3_i8",
                                          args, Call::PureExtern);
                    } else if (op->type.is_uint()) {
                        return Call::make(op->type, "halide_xtensa_extract_0_of_3_u8",
                                          args, Call::PureExtern);
                    }
                }
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Call *op) override {
        // TODO(vksnk): disable widening_load until correctness issue is fixed.
        // if (op->name == "halide_xtensa_slice_to_native") {
        //     if (const Cast *cast = op->args[0].as<Cast>()) {
        //         internal_assert(op->args.size() == 4);
        //         if (const Load *load = cast->value.as<Load>()) {
        //             Expr dense_ramp_base = strided_ramp_base(load->index, 1);

        //             if (dense_ramp_base.defined() && is_const_one(load->predicate) && (cast->type.is_int_or_uint()) && ((cast->type.bits() == 16) || (cast->type.bits() == 32)) && (load->type.is_int_or_uint()) && (2 * load->type.bits() == cast->type.bits())) {
        //                 // arg1 is an index and arg2 is a native vector size.
        //                 dense_ramp_base = dense_ramp_base + op->args[1] * op->args[2];
        //                 // The third argument is just to pass the type of load.
        //                 return Call::make(op->type, "halide_xtensa_widening_load", {Variable::make(type_of<void *>(), load->name), dense_ramp_base, make_one(load->type.element_of())}, Call::PureExtern);
        //             }
        //         }
        //     }
        // }

        // NOTE(vksnk): there seems to be a single instructions which could do lerp-like compute,
        // but documentation is confusing and I couldn't get it right, so need to revisit at some point.
        // if (op->is_intrinsic(Call::lerp) && op->type.is_int() && (op->type.bits() == 16) && (op->type.lanes() == 32)) {
        //   internal_assert(op->args.size() == 3);
        //   Expr weight = mutate(op->args[2]);
        //   const Broadcast* maybe_bc = weight.as<Broadcast>();
        //   if (maybe_bc) {
        //     weight = maybe_bc->value;
        //   }
        //   return Call::make(op->type, "halide_xtensa_lerp_i16",
        //                     {mutate(op->args[0]), mutate(op->args[1]), weight},
        //                     Call::PureExtern);
        // } else
        if (op->is_intrinsic(Call::lerp)) {
            // We need to lower lerps now to optimize the arithmetic
            // that they generate.
            internal_assert(op->args.size() == 3);
            return mutate(lower_lerp(op->type, op->args[0], op->args[1], op->args[2], target));
        } else if (op->is_intrinsic(Call::absd) && op->type.is_vector() && op->type.is_uint() && (op->type.bits() == 16)) {
            internal_assert(op->args.size() == 2);
            return Call::make(op->type, "halide_xtensa_absd_i16",
                              {mutate(op->args[0]), mutate(op->args[1])},
                              Call::PureExtern);
        } else if (op->is_intrinsic(Call::widening_shift_left)) {
            // Replace widening left shift with multiplication.
            const uint64_t *c = as_const_uint(op->args[1]);
            if (c && op->args[1].type().can_represent((uint64_t)1 << *c)) {
                if (op->args[0].type().is_int() && (*c < (uint64_t)op->args[0].type().bits() - 1)) {
                    return mutate(widening_mul(op->args[0], bc(IntImm::make(op->args[1].type().with_code(halide_type_int).with_lanes(1), (int64_t)1 << *c), op->args[1].type().lanes())));
                } else {
                    return mutate(widening_mul(op->args[0], bc(UIntImm::make(op->args[1].type().with_lanes(1), (uint64_t)1 << *c), op->args[1].type().lanes())));
                }
            }
        }

        int slice_width_i16 = target.natural_vector_size<int16_t>();
        int slice_width_i32 = target.natural_vector_size<int32_t>();

        static const std::vector<Pattern> calls = {
            {"halide_xtensa_abs_i8", abs(wild_i8x)},
            {"halide_xtensa_abs_i16", abs(wild_i16x)},
            {"halide_xtensa_abs_i32", abs(wild_i32x)},
            {"halide_xtensa_abs_f32", abs(wild_f32x)},

            {"halide_xtensa_avg_u8", halving_add(wild_u8x, wild_u8x)},
            {"halide_xtensa_avg_i8", halving_add(wild_i8x, wild_i8x)},

            {"halide_xtensa_avg_u16", halving_add(wild_u16x, wild_u16x)},
            {"halide_xtensa_avg_i16", halving_add(wild_i16x, wild_i16x)},

            // {"halide_xtensa_avg_u32", halving_add(wild_u32x, wild_u32x)},
            // {"halide_xtensa_avg_i32", halving_add(wild_i32x, wild_i32x)},

            {"halide_xtensa_avg_round_u8", rounding_halving_add(wild_u8x, wild_u8x)},
            {"halide_xtensa_avg_round_i8", rounding_halving_add(wild_i8x, wild_i8x)},

            {"halide_xtensa_avg_round_u16", rounding_halving_add(wild_u16x, wild_u16x)},
            {"halide_xtensa_avg_round_i16", rounding_halving_add(wild_i16x, wild_i16x)},

            // {"halide_xtensa_avg_round_u32", rounding_halving_add(wild_u32x, wild_u32x)},
            // {"halide_xtensa_avg_round_i32", rounding_halving_add(wild_i32x, wild_i32x)},

            {"halide_xtensa_sat_add_i16", saturating_add(wild_i16x, wild_i16x)},
            {"halide_xtensa_sat_add_i32", saturating_add(wild_i32x, wild_i32x)},
            {"halide_xtensa_sat_sub_i16", saturating_sub(wild_i16x, wild_i16x)},

            {"halide_xtensa_widen_mul_i24", widening_mul(wild_i8x, wild_i8x), Pattern::AccumulatorOutput24},
            {"halide_xtensa_widen_mul_u24", widening_mul(wild_u8x, wild_u8x), Pattern::AccumulatorOutput24},

            {"halide_xtensa_widen_mul_i48", widening_mul(wild_i16x, wild_i16x), Pattern::AccumulatorOutput48},
            {"halide_xtensa_widen_mul_ui48", widening_mul(wild_u16x, wild_i16x), Pattern::AccumulatorOutput48},
            {"halide_xtensa_widen_mul_ui48", widening_mul(wild_i16x, wild_u16x), Pattern::AccumulatorOutput48 | Pattern::SwapOps01},
            {"halide_xtensa_widen_mul_u48", widening_mul(wild_u16x, wild_u16x), Pattern::AccumulatorOutput48},
            {"halide_xtensa_widen_mul_i64", widening_mul(wild_i32x, wild_i32x), Pattern::AccumulatorOutput64},
            {"halide_xtensa_widen_mul_u64", widening_mul(wild_u32x, wild_u32x), Pattern::AccumulatorOutput64},

            {"halide_xtensa_widen_add_u48", widening_add(wild_u16x, wild_u16x), Pattern::AccumulatorOutput48},
            {"halide_xtensa_widen_add_i48", widening_add(wild_i16x, wild_i16x), Pattern::AccumulatorOutput48},

            {"halide_xtensa_widen_right_mul_u64", widen_right_mul(wild_u32x, wild_u16x), Pattern::AccumulatorOutput64},

            {"halide_xtensa_widen_zzzzz", halide_xtensa_widen_mul_u24(wild_u8x256, wild_u8)},
            {"halide_xtensa_widen_zzzzz", halide_xtensa_widen_mul_u24(concat({wild_u8x64, wild_u8x64, wild_u8x64, wild_u8x64}), repeat_each_element(wild_u8x4, 64))},
            {"halide_xtensa_widen_zzzzz", halide_xtensa_widen_mul_u24(repeat_each_element(wild_u8x4, 64), wild_u8x256), Pattern::SwapOps01},

            // {"halide_xtensa_rounding_mul_shift_right_i8", rounding_mul_shift_right(wild_i8x, wild_i8x, bc(wild_u8))},
            // {"halide_xtensa_rounding_mul_shift_right_i16", rounding_mul_shift_right(wild_i16x, wild_i16x, bc(wild_u16))},
            // {"halide_xtensa_rounding_mul_shift_right_i32", rounding_mul_shift_right(wild_i32x, wild_i32x, bc(wild_u32))},

            {"halide_xtensa_sat_narrow_with_rounding_shift_i8", i8_sat(rounding_shift_right(wild_i16x, wild_u16))},
            {"halide_xtensa_sat_narrow_with_rounding_shift_u8", u8_sat(rounding_shift_right(wild_i16x, wild_u16))},
            {"halide_xtensa_sat_narrow_with_rounding_shift_i16", i16_sat(rounding_shift_right(wild_i32x, wild_u32))},
            {"halide_xtensa_sat_narrow_with_rounding_shift_i32", i32_sat(rounding_shift_right(wild_i64x, wild_u64))},

            {"halide_xtensa_sat_narrow_with_signed_rounding_shift_i8", i8_sat(rounding_shift_right(wild_i16x, wild_i16))},
            {"halide_xtensa_sat_narrow_with_signed_rounding_shift_u8", u8_sat(rounding_shift_right(wild_i16x, wild_i16))},
            {"halide_xtensa_sat_narrow_with_signed_rounding_shift_i16", i16_sat(rounding_shift_right(wild_i32x, wild_i32))},
            {"halide_xtensa_sat_narrow_with_signed_rounding_shift_i32", i32_sat(rounding_shift_right(wild_i64x, wild_i64))},

            {"halide_xtensa_sat_left_shift_i16", i16_sat(widening_shift_left(wild_i16x, wild_i16x))},
            {"halide_xtensa_sat_left_shift_i16", i16_sat(widening_shift_left(wild_i16x, wild_u16x))},

            {"halide_xtensa_sat_left_shift_i32", i32_sat(widening_shift_left(wild_i32x, wild_i32x))},
            {"halide_xtensa_sat_left_shift_i32", i32_sat(widening_shift_left(wild_i32x, wild_u32x))},

            {"halide_xtensa_sat_narrow_shift_i32", i32_sat(wild_i64x >> bc(wild_i64))},
            {"halide_xtensa_sat_narrow_shift_i32", i32_sat(wild_i64x / bc(wild_i64)), Pattern::ExactLog2Op1},
            {"halide_xtensa_sat_narrow_shift_i32", i32_sat(wild_i64x >> bc(wild_u64))},
            {"halide_xtensa_sat_narrow_shift_i32", i32_sat(wild_i64x / bc(wild_u64)), Pattern::ExactLog2Op1},

            {"halide_xtensa_sat_narrow_i24x_with_shift_u8", u8_sat(i16(wild_i24x) >> bc(wild_i16))},
            {"halide_xtensa_sat_narrow_i24x_with_shift_u8", u8_sat(i16(wild_i24x) / bc(wild_i16)), Pattern::ExactLog2Op1},

            {"halide_xtensa_sat_narrow_i8", i8_sat(wild_i16x)},
            {"halide_xtensa_sat_narrow_u8", u8_sat(wild_i16x)},
            {"halide_xtensa_sat_narrow_i16", i16_sat(wild_i32x)},

            {"halide_xtensa_rounding_shift_right_i8", rounding_shift_right(wild_i8x, bc(wild_u8))},
            // {"halide_xtensa_rounding_shift_right_u8", rounding_shift_right(wild_u8x, bc(wild_u8))},
            {"halide_xtensa_rounding_shift_right_i16", rounding_shift_right(wild_i16x, bc(wild_u16))},
            // {"halide_xtensa_rounding_shift_right_u16", rounding_shift_right(wild_u16x, bc(wild_u16))},
            {"halide_xtensa_rounding_shift_right_i32", rounding_shift_right(wild_i32x, bc(wild_u32))},
            // {"halide_xtensa_rounding_shift_right_u32", rounding_shift_right(wild_u32x, bc(wild_u32))},

            {"halide_xtensa_narrow_i48_with_shift_i16", call("halide_xtensa_narrow_with_shift_i16", wild_i16x, {i32(wild_i48x), wild_i32})},
            {"halide_xtensa_narrow_i48_with_rounding_shift_i16", call("halide_xtensa_narrow_with_rounding_shift_i16", wild_i16x, {i32(wild_i48x), wild_u32})},

            {"halide_xtensa_widen_pair_mul_add_u24",
             call("halide_xtensa_yyyy", wild_i24x, {wild_i24x, halide_xtensa_concat_from_native_i24(halide_xtensa_widen_mul_u24(wild_u8x, wild_u8x), halide_xtensa_widen_mul_u24(wild_u8x, wild_u8x))})},

            {"halide_xtensa_widen_quad_mul_add_i24",
             call("halide_xtensa_yyyy", wild_i24x, {wild_i24x, call("halide_xtensa_qqqq", wild_i24x, {call("halide_xtensa_widen_zzzzz", wild_i24x, {wild_i8x, wild_i8x, wild_i8x, wild_i8x, wild_i8x})})})},

            {"halide_xtensa_widen_quad_mul_add_i24",
             call("halide_xtensa_yyyy", wild_i24x, {wild_i24x, call("halide_xtensa_qqqq", wild_i24x, {call("halide_xtensa_widen_zzzzz", wild_i24x, {wild_i8x256, wild_i8x4})})})},

            {"halide_xtensa_widen_quad_mul_add_u24",
             call("halide_xtensa_yyyy", wild_i24x, {wild_i24x, call("halide_xtensa_qqqq", wild_i24x, {call("halide_xtensa_widen_zzzzz", wild_i24x, {wild_u8x, wild_u8x, wild_u8x, wild_u8x, wild_u8x})})})},

            {"halide_xtensa_widen_quad_mul_add_u24",
             call("halide_xtensa_yyyy", wild_i24x, {wild_i24x, call("halide_xtensa_qqqq", wild_i24x, {call("halide_xtensa_widen_zzzzz", wild_i24x, {wild_u8x256, wild_u8x4})})})},

            {"halide_xtensa_widen_quad_mul_add_by_scalar_u24",
             call("halide_xtensa_yyyy", wild_i24x, {wild_i24x, call("halide_xtensa_qqqq", wild_i24x, {call("halide_xtensa_widen_zzzzz", wild_i24x, {wild_u8x256, wild_u8})})})},

            {"halide_xtensa_widen_quad_mul_add_i24",
             call("halide_xtensa_widen_pair_mul_add_i24", wild_i24x, {call("halide_xtensa_widen_pair_mul_add_i24", wild_i24x, {wild_i24x, wild_i8x, wild_i8, wild_i8x, wild_i8}), wild_i8x, wild_i8, wild_i8x, wild_i8})},
            {"halide_xtensa_widen_pair_mul_add_i24",
             call("halide_xtensa_widen_mul_add_i24", wild_i24x, {call("halide_xtensa_widen_mul_add_i24", wild_i24x, {wild_i24x, wild_i8x, wild_i8}), wild_i8x, wild_i8})},

            {"halide_xtensa_widen_pair_mul_add_i48",
             call("halide_xtensa_widen_mul_add_i48", wild_i48x,
                  {call("halide_xtensa_widen_mul_add_i48", wild_i48x, {wild_i48x, wild_i16x, wild_i16x}), wild_i16x, wild_i16x})},

            {"halide_xtensa_sat_narrow_i48_with_shift_i16", call("halide_xtensa_sat_narrow_with_rounding_shift_i16", wild_i16x, {i32(wild_i48x), wild_u32})},
            // NOTE(vksnk): looked like a good idea, but seems to be slower. Need to double-check.
            // {"halide_xtensa_i48x_clz_i16", halide_xtensa_narrow_clz_i16(i32(wild_i48x))},
            // {"halide_xtensa_i48x_clz_i16", halide_xtensa_narrow_clz_i16(u32(wild_i48x))},
            // Slice and convert
            {"halide_xtensa_convert_u8_low_u16", halide_xtensa_slice_to_native_u16(u16(wild_u8x), 0, wild_i32, wild_i32)},
            {"halide_xtensa_convert_u8_high_u16", halide_xtensa_slice_to_native_u16(u16(wild_u8x), 1, wild_i32, wild_i32)},
            {"halide_xtensa_convert_u8_low_i16", halide_xtensa_slice_to_native_i16(i16(wild_u8x), 0, wild_i32, wild_i32)},
            {"halide_xtensa_convert_u8_high_i16", halide_xtensa_slice_to_native_i16(i16(wild_u8x), 1, wild_i32, wild_i32)},
            {"halide_xtensa_convert_i8_low_u16", halide_xtensa_slice_to_native_u16(u16(wild_i8x), 0, wild_i32, wild_i32)},
            {"halide_xtensa_convert_i8_high_u16", halide_xtensa_slice_to_native_u16(u16(wild_i8x), 1, wild_i32, wild_i32)},
            {"halide_xtensa_convert_i8_low_i16", halide_xtensa_slice_to_native_i16(i16(wild_i8x), 0, wild_i32, wild_i32)},
            {"halide_xtensa_convert_i8_high_i16", halide_xtensa_slice_to_native_i16(i16(wild_i8x), 1, wild_i32, wild_i32)},
            {"halide_xtensa_convert_i32_u16", halide_xtensa_slice_to_native_u16(u16(halide_xtensa_concat_from_native_i32(wild_i32x, wild_i32x, wild_i32x, wild_i32x)), 0, slice_width_i16, slice_width_i16 * 2), Pattern::PassOnlyOp0 | Pattern::PassOnlyOp1},
            {"halide_xtensa_convert_i32_u16", halide_xtensa_slice_to_native_u16(u16(halide_xtensa_concat_from_native_i32(wild_i32x, wild_i32x, wild_i32x, wild_i32x)), 1, slice_width_i16, slice_width_i16 * 2), Pattern::PassOnlyOp2 | Pattern::PassOnlyOp3},

            {"halide_xtensa_convert_i48_low_i32", halide_xtensa_slice_to_native_i32(i32(wild_i48x), 0, slice_width_i32, slice_width_i32 * 2)},
            {"halide_xtensa_convert_i48_high_i32", halide_xtensa_slice_to_native_i32(i32(wild_i48x), 1, slice_width_i32, slice_width_i32 * 2)},
            {"halide_xtensa_convert_i48_low_i32", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_i48(wild_i48x, wild_i48x)), 0, slice_width_i32, slice_width_i32 * 4), Pattern::PassOnlyOp0},
            {"halide_xtensa_convert_i48_high_i32", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_i48(wild_i48x, wild_i48x)), 1, slice_width_i32, slice_width_i32 * 4), Pattern::PassOnlyOp0},
            {"halide_xtensa_convert_i48_low_i32", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_i48(wild_i48x, wild_i48x)), 2, slice_width_i32, slice_width_i32 * 4), Pattern::PassOnlyOp1},
            {"halide_xtensa_convert_i48_high_i32", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_i48(wild_i48x, wild_i48x)), 3, slice_width_i32, slice_width_i32 * 4), Pattern::PassOnlyOp1},
            {"halide_xtensa_convert_i48_low_u32", halide_xtensa_slice_to_native_u32(u32(wild_i48x), 0, slice_width_i32, slice_width_i32 * 2)},
            {"halide_xtensa_convert_i48_high_u32", halide_xtensa_slice_to_native_u32(u32(wild_i48x), 1, slice_width_i32, slice_width_i32 * 2)},

            {"halide_xtensa_convert_u16_low_u32", halide_xtensa_slice_to_native_u32(u32(wild_u16x), 0, slice_width_i32, slice_width_i32 * 2)},
            {"halide_xtensa_convert_u16_high_u32", halide_xtensa_slice_to_native_u32(u32(wild_u16x), 1, slice_width_i32, slice_width_i32 * 2)},
            {"halide_xtensa_convert_u16_low_i32", halide_xtensa_slice_to_native_i32(i32(wild_u16x), 0, slice_width_i32, slice_width_i32 * 2)},
            {"halide_xtensa_convert_u16_high_i32", halide_xtensa_slice_to_native_i32(i32(wild_u16x), 1, slice_width_i32, slice_width_i32 * 2)},
            {"halide_xtensa_convert_i16_low_u32", halide_xtensa_slice_to_native_u32(u32(wild_i16x), 0, slice_width_i32, slice_width_i32 * 2)},
            {"halide_xtensa_convert_i16_high_u32", halide_xtensa_slice_to_native_u32(u32(wild_i16x), 1, slice_width_i32, slice_width_i32 * 2)},
            {"halide_xtensa_convert_i16_low_i32", halide_xtensa_slice_to_native_i32(i32(wild_i16x), 0, slice_width_i32, slice_width_i32 * 2)},
            {"halide_xtensa_convert_i16_high_i32", halide_xtensa_slice_to_native_i32(i32(wild_i16x), 1, slice_width_i32, slice_width_i32 * 2)},

            {"halide_xtensa_convert_to_int32x16_t_from_uint1x16_t", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_u1(wild_u1x, wild_u1x, wild_u1x, wild_u1x)), 0, 16, 64), Pattern::PassOnlyOp0},
            {"halide_xtensa_convert_to_int32x16_t_from_uint1x16_t", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_u1(wild_u1x, wild_u1x, wild_u1x, wild_u1x)), 1, 16, 64), Pattern::PassOnlyOp1},
            {"halide_xtensa_convert_to_int32x16_t_from_uint1x16_t", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_u1(wild_u1x, wild_u1x, wild_u1x, wild_u1x)), 2, 16, 64), Pattern::PassOnlyOp2},
            {"halide_xtensa_convert_to_int32x16_t_from_uint1x16_t", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_u1(wild_u1x, wild_u1x, wild_u1x, wild_u1x)), 3, 16, 64), Pattern::PassOnlyOp3},

            {"halide_xtensa_narrow_i48_with_shift_i32", i32(wild_i48x) >> wild_i32},
            {"halide_xtensa_narrow_i48_with_shift_u32", u32(wild_i48x) >> wild_u32},

            // Predicated saturated add/sub.
            // NOTE(vksnk): patterns below are for predicated instructions and look like they may
            // be more efficient, but they are not according to simulator. We will need to check with
            // Cadence about this.
            // {"halide_xtensa_pred_sat_add_i16", halide_xtensa_sat_add_i16(wild_i16x, select(wild_u1x, wild_i16x, wild_i16x))},
            // {"halide_xtensa_pred_sat_sub_i16", halide_xtensa_sat_sub_i16(wild_i16x, select(wild_u1x, wild_i16x, wild_i16x))},
        };
        if (op->type.is_vector()) {
            Expr call = op;

            std::vector<Expr> matches;

            Expr new_expr = apply_patterns(call, calls, this);
            if (!new_expr.same_as(call)) {
                return new_expr;
            }
        }

        if (op->is_intrinsic()) {
            Expr lowered = lower_intrinsic(op);
            if (lowered.defined()) {
                debug(1) << "Lowered intrinsic - " << op->name << "\n";
                // lowered = simplify(lowered);
                return mutate(lowered);
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const VectorReduce *op) override {
        if (op->value.type().lanes() == op->type.lanes() * 2) {
            static const std::vector<Pattern> reduces_2x = {
                {"halide_xtensa_reduce_add_x2_i8", vector_reduce(VectorReduce::Add, wild_i16x), Pattern::NarrowOps},
                {"halide_xtensa_reduce_add_x2_i16", vector_reduce(VectorReduce::Add, wild_i32x), Pattern::NarrowOps},
                {"halide_xtensa_reduce_add_x2_i32", vector_reduce(VectorReduce::Add, wild_i32x)},
            };

            Expr new_expr = apply_patterns(op, reduces_2x, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        if (op->value.type().lanes() == op->type.lanes() * 4) {
            static const std::vector<Pattern> reduces_4x = {
                {"halide_xtensa_reduce_add_x4_i8", vector_reduce(VectorReduce::Add, wild_i16x), Pattern::NarrowOps},
                {"halide_xtensa_reduce_add_x4_i16", vector_reduce(VectorReduce::Add, wild_i32x), Pattern::NarrowOps},
                {"halide_xtensa_reduce_add_x4_i32", vector_reduce(VectorReduce::Add, wild_i32x)},
            };

            Expr new_expr = apply_patterns(op, reduces_4x, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        // Full reduction.
        if (op->type.is_scalar()) {
            static const std::vector<Pattern> full_reduces = {
                // TODO(vksnk): should be a better way to do the cast in the end.
                {"halide_xtensa_full_reduce_add_u8_to_i32", vector_reduce(VectorReduce::Add, i32(wild_u8x))},

                {"halide_xtensa_full_reduce_add_i8", vector_reduce(VectorReduce::Add, wild_i16x), Pattern::NarrowOps},
                {"halide_xtensa_full_reduce_add_i16", vector_reduce(VectorReduce::Add, wild_i32x), Pattern::NarrowOps},
                {"halide_xtensa_full_reduce_add_i32", vector_reduce(VectorReduce::Add, wild_i32x)},

                // Min reduction.
                {"halide_xtensa_full_reduce_min_u8", vector_reduce(VectorReduce::Min, wild_u8x)},
                {"halide_xtensa_full_reduce_min_u16", vector_reduce(VectorReduce::Min, wild_u16x)},
                {"halide_xtensa_full_reduce_min_u32", vector_reduce(VectorReduce::Min, wild_u32x)},
                {"halide_xtensa_full_reduce_min_i8", vector_reduce(VectorReduce::Min, wild_i8x)},
                {"halide_xtensa_full_reduce_min_i16", vector_reduce(VectorReduce::Min, wild_i16x)},
                {"halide_xtensa_full_reduce_min_i32", vector_reduce(VectorReduce::Min, wild_i32x)},

                // Max reduction.
                {"halide_xtensa_full_reduce_max_u8", vector_reduce(VectorReduce::Max, wild_u8x)},
                {"halide_xtensa_full_reduce_max_u16", vector_reduce(VectorReduce::Max, wild_u16x)},
                {"halide_xtensa_full_reduce_max_u32", vector_reduce(VectorReduce::Max, wild_u32x)},
                {"halide_xtensa_full_reduce_max_i8", vector_reduce(VectorReduce::Max, wild_i8x)},
                {"halide_xtensa_full_reduce_max_i16", vector_reduce(VectorReduce::Max, wild_i16x)},
                {"halide_xtensa_full_reduce_max_i32", vector_reduce(VectorReduce::Max, wild_i32x)},
            };

            Expr new_expr = apply_patterns(op, full_reduces, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    int loop_depth_ = 0;

    Stmt visit(const For *op) override {
        loop_depth_++;
        Stmt body = IRGraphMutator::visit(op);
        loop_depth_--;
        return body;
    }

    Stmt visit(const LetStmt *op) override {
        if (loop_depth_ < 1) {
            return IRGraphMutator::visit(op);
        }

        if (op->value.type().is_handle()) {
            return IRGraphMutator::visit(op);
        }

        if (op->value.type().is_scalar()) {
            return IRGraphMutator::visit(op);
        }
        Stmt body = op->body;
        body = substitute(op->name, op->value, body);
        return mutate(body);
    }

    Expr match_clamped_dense_ramp(const Expr &index, const Expr &pred) {
        Expr dense_ramp_base = strided_ramp_base(index, 1);
        if (!dense_ramp_base.defined()) {
            return Expr();
        }

        const std::vector<Expr> patterns = {
            ramp(wild_i32, 1, pred.type().lanes()) <= bc(wild_i32, pred.type().lanes())};

        vector<Expr> matches;
        Expr new_pred;
        for (const Expr &p : patterns) {
            if (expr_match(p, pred, matches)) {
                for (auto &m : matches) {
                    m = mutate(m);
                }
                new_pred = Call::make(pred.type(), "clamped_dense_ramp", matches, Call::PureExtern);
                break;
            }
        }
        return new_pred;
    }

    Expr visit(const Load *op) override {
        if (!is_const_one(op->predicate)) {
            Expr new_pred = match_clamped_dense_ramp(op->index, op->predicate);

            if (new_pred.defined()) {
                return Load::make(op->type, op->name,
                                  mutate(op->index), op->image,
                                  op->param,
                                  new_pred,
                                  op->alignment);
            }
        }

        return IRGraphMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        if (!is_const_one(op->predicate)) {
            Expr new_pred = match_clamped_dense_ramp(op->index, op->predicate);

            if (new_pred.defined()) {
                return Store::make(op->name, mutate(op->value), mutate(op->index),
                                   op->param, new_pred, op->alignment);
            }
        }

        return IRGraphMutator::visit(op);
    }

public:
    MatchXtensaPatterns(const Target &target)
        : target(target) {
    }
};

// Find an upper bound of bounds.max - bounds.min.
Expr span_of_bounds(const Interval &bounds) {
    internal_assert(bounds.is_bounded());

    const Min *min_min = bounds.min.as<Min>();
    const Max *min_max = bounds.min.as<Max>();
    const Min *max_min = bounds.max.as<Min>();
    const Max *max_max = bounds.max.as<Max>();
    const Add *min_add = bounds.min.as<Add>();
    const Add *max_add = bounds.max.as<Add>();
    const Sub *min_sub = bounds.min.as<Sub>();
    const Sub *max_sub = bounds.max.as<Sub>();

    if (min_min && max_min && equal(min_min->b, max_min->b)) {
        return span_of_bounds({min_min->a, max_min->a});
    } else if (min_max && max_max && equal(min_max->b, max_max->b)) {
        return span_of_bounds({min_max->a, max_max->a});
    } else if (min_add && max_add && equal(min_add->b, max_add->b)) {
        return span_of_bounds({min_add->a, max_add->a});
    } else if (min_sub && max_sub && equal(min_sub->b, max_sub->b)) {
        return span_of_bounds({min_sub->a, max_sub->a});
    } else {
        return bounds.max - bounds.min;
    }
}

// NOTE(vksnk): this is borrowed from HexagonOptimize.cpp, so
// eventually need to generalize and share across two places.
// Replace indirect loads with dynamic_shuffle intrinsics where
// possible.
class OptimizeShuffles : public IRMutator {
    int lut_alignment;
    int lut_size_in_bytes;
    Scope<Interval> bounds;
    std::vector<std::pair<std::string, Expr>> lets;

    using IRMutator::visit;

    template<typename NodeType, typename T>
    NodeType visit_let(const T *op) {
        // We only care about vector lets.
        if (op->value.type().is_vector()) {
            bounds.push(op->name, bounds_of_expr_in_scope(op->value, bounds));
        }
        NodeType node = IRMutator::visit(op);
        if (op->value.type().is_vector()) {
            bounds.pop(op->name);
        }
        return node;
    }

    Expr visit(const Let *op) override {
        lets.emplace_back(op->name, op->value);
        Expr expr = visit_let<Expr>(op);
        lets.pop_back();
        return expr;
    }
    Stmt visit(const LetStmt *op) override {
        return visit_let<Stmt>(op);
    }

    Expr visit(const Load *op) override {
        if (!is_const_one(op->predicate)) {
            // TODO(psuriana): We shouldn't mess with predicated load for now.
            return IRMutator::visit(op);
        }
        if (!op->type.is_vector() || op->index.as<Ramp>()) {
            // Don't handle scalar or simple vector loads.
            return IRMutator::visit(op);
        }

        Expr index = mutate(op->index);
        Interval unaligned_index_bounds = bounds_of_expr_in_scope(index, bounds);
        if (unaligned_index_bounds.is_bounded()) {
            // We want to try both the unaligned and aligned
            // bounds. The unaligned bounds might fit in 64 elements,
            // while the aligned bounds do not.
            int align = lut_alignment / op->type.bytes();
            Interval aligned_index_bounds = {
                (unaligned_index_bounds.min / align) * align,
                ((unaligned_index_bounds.max + align) / align) * align - 1};
            ModulusRemainder alignment(align, 0);

            for (const Interval &index_bounds : {aligned_index_bounds, unaligned_index_bounds}) {
                Expr index_span = span_of_bounds(index_bounds);
                index_span = common_subexpression_elimination(index_span);
                index_span = simplify(index_span);

                // The hardware supports shuffle/select out of two native vectors,
                // so we set to the double of native vector width in bytes.
                // TODO(vksnk): in some cases it might be possible to prove that
                // all indices span only a single vector (instead of two which is
                // assumed here, which may help to save one vector load.
                int lut_size = lut_size_in_bytes / op->type.element_of().bytes();
                if (can_prove(index_span < lut_size)) {
                    // This is a lookup within an up to 64 element array. We
                    // can use dynamic_shuffle for this.
                    // TODO(vksnk): original code doesn't align/pad here, why?
                    int const_extent = as_const_int(index_span) ? (((*as_const_int(index_span) + align) / align) * align) : lut_size;
                    Expr base = simplify(index_bounds.min);

                    // Load all of the possible indices loaded from the
                    // LUT. Note that for clamped ramps, this loads up to 1
                    // vector past the max. CodeGen_Hexagon::allocation_padding
                    // returns a native vector size to account for this.
                    Expr lut = Load::make(op->type.with_lanes(const_extent), op->name,
                                          Ramp::make(base, 1, const_extent),
                                          op->image, op->param, const_true(const_extent), alignment);

                    // We know the size of the LUT is not more than 64, so we
                    // can safely cast the index to 16 bit, which
                    // dynamic_shuffle requires.
                    index = simplify(cast(Int(op->type.bits()).with_lanes(op->type.lanes()), index - base));
                    return Call::make(op->type, "halide_xtensa_dynamic_shuffle", {lut, index /*, 0, const_extent - 1*/}, Call::PureExtern);
                }
                // Only the first iteration of this loop is aligned.
                alignment = ModulusRemainder();
            }
        }
        if (!index.same_as(op->index)) {
            return Load::make(op->type, op->name, index, op->image, op->param, op->predicate, op->alignment);
        } else {
            return op;
        }
    }

public:
    OptimizeShuffles(int alignment, int size_in_bytes)
        : lut_alignment(alignment), lut_size_in_bytes(size_in_bytes) {
    }
};

class SplitVectorsToNativeSizes : public IRMutator {
private:
    std::vector<Type> native_vector_types;

    using IRMutator::visit;

    // Checks the list of native_vector_types and returns native vector width if the given type
    // is multiple of it.
    int get_native_vector_lanes_num(const Type &type) {
        for (const auto &t : native_vector_types) {
            if ((t.code() == type.code()) && (t.bits() == type.bits()) && (type.lanes() > t.lanes()) && (type.lanes() % t.lanes() == 0)) {
                return t.lanes();
            }
        }
        return 0;
    }

    int get_width_to_extend(const Type &type) {
        if (!type.is_vector()) {
            return 0;
        }

        for (const auto &t : native_vector_types) {
            if ((t.code() == type.code()) && (t.bits() == type.bits()) && (type.lanes() < t.lanes())) {
                return t.lanes();
            }
        }
        return 0;
    }

    Expr pad(const Expr &e, int old_lanes, int new_lanes) {
        return Call::make(e.type().with_lanes(new_lanes),
                          "halide_xtensa_pad_to_native",
                          {e, old_lanes},
                          Call::PureExtern);
        // TODO(vksnk): we should be able to use regular concats and slices
        // but codegen support of non-uniform shuffles is limited right now.
        // return Shuffle::make_concat({e, make_one(e.type().with_lanes(new_lanes - old_lanes))});
    }

    Expr slice(Expr e, Type t, int lanes) {
        return Call::make(t, "halide_xtensa_slice_from_padded",
                          {std::move(e), lanes}, Call::PureExtern);
        // return Shuffle::make_slice(e, 0, 1, lanes);
    }

    Expr visit(const Broadcast *op) override {
        int native_lanes = get_native_vector_lanes_num(op->type);
        if (native_lanes > 0) {
            int split_to = op->type.lanes() / native_lanes;
            Expr value = mutate(op->value);

            std::vector<Expr> concat_args;
            for (int ix = 0; ix < split_to; ix++) {
                Expr r = Broadcast::make(value, native_lanes);
                concat_args.push_back(std::move(r));
            }
            return Call::make(op->type,
                              "halide_xtensa_concat_from_native",
                              concat_args, Call::PureExtern);
        }

        return IRMutator::visit(op);
    }

    Expr visit(const Select *op) override {
        int native_lanes = get_native_vector_lanes_num(op->type);
        if (native_lanes > 0) {
            const int total_lanes = op->type.lanes();
            int split_to = op->type.lanes() / native_lanes;
            Expr cond = mutate(op->condition);
            Expr t = mutate(op->true_value);
            Expr f = mutate(op->false_value);

            std::vector<Expr> concat_args;
            for (int ix = 0; ix < split_to; ix++) {
                Expr sliced_cond = Call::make(cond.type().with_lanes(native_lanes),
                                              "halide_xtensa_slice_to_native",
                                              {cond, ix, native_lanes, total_lanes},
                                              Call::PureExtern);
                Expr sliced_t = Call::make(t.type().with_lanes(native_lanes),
                                           "halide_xtensa_slice_to_native",
                                           {t, ix, native_lanes, total_lanes},
                                           Call::PureExtern);
                Expr sliced_f = Call::make(f.type().with_lanes(native_lanes),
                                           "halide_xtensa_slice_to_native",
                                           {f, ix, native_lanes, total_lanes},
                                           Call::PureExtern);
                Expr r = Select::make(sliced_cond, sliced_t, sliced_f);
                concat_args.push_back(std::move(r));
            }
            return Call::make(op->type,
                              "halide_xtensa_concat_from_native",
                              concat_args, Call::PureExtern);
        }

        int width_to_extend = get_width_to_extend(op->type);
        if (width_to_extend > 0) {
            const int lanes = op->type.lanes();

            Expr cond = mutate(op->condition);
            Expr t = mutate(op->true_value);
            Expr f = mutate(op->false_value);

            Expr padded_cond = pad(cond, lanes, width_to_extend);
            Expr padded_t = pad(t, lanes, width_to_extend);
            Expr padded_f = pad(f, lanes, width_to_extend);

            Expr r = Select::make(padded_cond, padded_t, padded_f);

            return slice(r, op->type, lanes);
        }

        return IRMutator::visit(op);
    }

    // NOTE(vksnk): not very clear if it's a good idea to slice loads/stores.
    // Expr visit(const Load* op) override {
    //     debug(0) << "maybe slicing load" << op->index << "\n";
    //     Expr dense_ramp_base = strided_ramp_base(op->index, 1);
    //     if (dense_ramp_base.defined()) {
    //         const int64_t *const_base_ptr = as_const_int(dense_ramp_base);
    //         if (const_base_ptr && is_const_one(op->predicate)) {
    //             int native_lanes = get_native_vector_lanes_num(op->type);
    //             int split_to = op->type.lanes() / native_lanes;
    //             // Expr predicate = mutate(op->predicate);
    //             // Expr ramp_base = mutate(op->index.as<Ramp>()->base);
    //             // Expr index = Ramp::make(ramp_base, 1, op->index.type().lanes());
    //             int64_t const_base = *const_base_ptr;
    //             std::vector<Expr> concat_args;
    //             for (int ix = 0; ix < split_to; ix++) {
    //                 concat_args.push_back(
    //                     Load::make(op->type.with_lanes(native_lanes),  op->name,
    //                             Ramp::make(Expr((int32_t)const_base + ix * native_lanes), Expr(1), native_lanes),
    //                             op->image, op->param, make_one(op->predicate.type().with_lanes(native_lanes)),
    //                             op->alignment + native_lanes));
    //             }

    //             return Call::make(op->type,
    //                         "halide_xtensa_concat_from_native",
    //                         concat_args, Call::PureExtern);
    //         }
    //     }
    //     return IRMutator::visit(op);
    // }

    //     Stmt visit(const Store* op) {
    //         Expr dense_ramp_base = strided_ramp_base(op->index, 1);
    //         if (dense_ramp_base.defined()) {
    //             Expr predicate = mutate(op->predicate);
    //             Expr value = mutate(op->value);
    //             Expr ramp_base = mutate(op->index.as<Ramp>()->base);
    //             Expr index = Ramp::make(ramp_base, 1, op->index.type().lanes());
    //             return Store::make(op->name, std::move(value), std::move(index), op->param, std::move(predicate), op->alignment);
    //         }
    //         return IRMutator::visit(op);
    //     }

    // Expr visit(const Ramp *op) override {
    //     int native_lanes = get_native_vector_lanes_num(op->type);
    //     if (native_lanes > 0) {
    //         int split_to = op->type.lanes() / native_lanes;
    //         Expr base = mutate(op->base);
    //         Expr stride = mutate(op->stride);

    //         std::vector<Expr> concat_args;
    //         for (int ix = 0; ix < split_to; ix++) {
    //             Expr r = Ramp::make(base + stride * (native_lanes * ix), stride, native_lanes);
    //             concat_args.push_back(std::move(r));
    //         }
    //         return Call::make(op->type,
    //                             "halide_xtensa_concat_from_native",
    //                             concat_args, Call::PureExtern);
    //     }
    //     int width_to_extend = get_width_to_extend(op->type);
    //     if (width_to_extend > 0) {
    //         Expr base = mutate(op->base);
    //         Expr stride = mutate(op->stride);

    //         const int lanes = op->type.lanes();
    //         Expr r = Ramp::make(base, stride, width_to_extend);

    //         return slice(r, op->type, lanes);
    //     }

    //     return IRMutator::visit(op);
    // }

    Expr visit(const Cast *op) override {
        int to_native_lanes = get_native_vector_lanes_num(op->type);
        int from_native_lanes = get_native_vector_lanes_num(op->value.type());
        int native_lanes = std::max(to_native_lanes, from_native_lanes);

        if ((to_native_lanes > 0) && (from_native_lanes > 0) && (native_lanes < op->type.lanes())) {
            const int total_lanes = op->type.lanes();
            int split_to = op->type.lanes() / native_lanes;

            Expr value = mutate(op->value);

            std::vector<Expr> concat_args;
            for (int ix = 0; ix < split_to; ix++) {
                Expr sliced = Call::make(value.type().with_lanes(native_lanes),
                                         "halide_xtensa_slice_to_native",
                                         {value, ix, native_lanes, total_lanes},
                                         Call::PureExtern);
                Expr r = Cast::make(op->type.with_lanes(native_lanes), sliced);
                concat_args.push_back(std::move(r));
            }
            return Call::make(op->type,
                              "halide_xtensa_concat_from_native",
                              concat_args, Call::PureExtern);
        }

        int width_to_extend = std::max(get_width_to_extend(op->type), get_width_to_extend(op->value.type()));
        if (width_to_extend > 0) {
            Expr value = mutate(op->value);

            const int lanes = op->type.lanes();
            Expr padded = pad(value, lanes, width_to_extend);
            Expr r = Cast::make(op->type.with_lanes(width_to_extend), padded);

            return slice(r, op->type, lanes);
        }

        return IRMutator::visit(op);
    }

    Expr visit(const Reinterpret *op) override {
        int to_native_lanes = get_native_vector_lanes_num(op->type);
        int from_native_lanes = get_native_vector_lanes_num(op->value.type());
        int native_lanes = std::max(to_native_lanes, from_native_lanes);

        if ((to_native_lanes > 0) && (from_native_lanes > 0) && (native_lanes < op->type.lanes())) {
            const int total_lanes = op->type.lanes();
            int split_to = op->type.lanes() / native_lanes;

            Expr value = mutate(op->value);

            std::vector<Expr> concat_args;
            for (int ix = 0; ix < split_to; ix++) {
                Expr sliced = Call::make(value.type().with_lanes(native_lanes),
                                         "halide_xtensa_slice_to_native",
                                         {value, ix, native_lanes, total_lanes},
                                         Call::PureExtern);
                Expr r = Reinterpret::make(op->type.with_lanes(native_lanes), sliced);
                concat_args.push_back(std::move(r));
            }
            return Call::make(op->type,
                              "halide_xtensa_concat_from_native",
                              concat_args, Call::PureExtern);
        }

        return IRMutator::visit(op);
    }

    template<typename Op>
    Expr visit_binop(const Op *op) {
        int native_lanes = get_native_vector_lanes_num(op->a.type());
        if (native_lanes > 0) {
            const int total_lanes = op->type.lanes();
            int split_to = op->type.lanes() / native_lanes;
            Expr a = mutate(op->a);
            Expr b = mutate(op->b);

            std::vector<Expr> concat_args;
            for (int ix = 0; ix < split_to; ix++) {
                Expr sliced_a = Call::make(a.type().with_lanes(native_lanes),
                                           "halide_xtensa_slice_to_native",
                                           {a, ix, native_lanes, total_lanes},
                                           Call::PureExtern);
                Expr sliced_b = Call::make(b.type().with_lanes(native_lanes),
                                           "halide_xtensa_slice_to_native",
                                           {b, ix, native_lanes, total_lanes},
                                           Call::PureExtern);
                Expr r = Op::make(sliced_a, sliced_b);
                concat_args.push_back(std::move(r));
            }
            return Call::make(op->type,
                              "halide_xtensa_concat_from_native",
                              concat_args, Call::PureExtern);
        }

        // TODO(vksnk): bool handling is maybe sketchy.
        int width_to_extend = op->type.is_bool() ? get_width_to_extend(op->a.type()) : get_width_to_extend(op->type);
        if (width_to_extend > 0) {
            Expr a = mutate(op->a);
            Expr b = mutate(op->b);

            const int lanes = op->type.lanes();

            Expr padded_a = pad(a, lanes, width_to_extend);
            Expr padded_b = pad(b, lanes, width_to_extend);
            Expr r = Op::make(padded_a, padded_b);

            return slice(r, op->type, lanes);
        }

        return IRMutator::visit(op);
    }

    Expr visit(const Add *op) override {
        return visit_binop(op);
    }

    Expr visit(const Sub *op) override {
        return visit_binop(op);
    }

    Expr visit(const Mul *op) override {
        return visit_binop(op);
    }

    Expr visit(const Div *op) override {
        return visit_binop(op);
    }

    Expr visit(const Mod *op) override {
        return visit_binop(op);
    }

    Expr visit(const Min *op) override {
        return visit_binop(op);
    }

    Expr visit(const Max *op) override {
        return visit_binop(op);
    }

    Expr visit(const EQ *op) override {
        return visit_binop(op);
    }

    Expr visit(const NE *op) override {
        return visit_binop(op);
    }

    Expr visit(const LT *op) override {
        return visit_binop(op);
    }

    Expr visit(const LE *op) override {
        return visit_binop(op);
    }

    Expr visit(const GT *op) override {
        return visit_binop(op);
    }

    Expr visit(const GE *op) override {
        return visit_binop(op);
    }

    Expr visit(const Or *op) override {
        return visit_binop(op);
    }

    Expr visit(const And *op) override {
        return visit_binop(op);
    }

    Expr visit(const Call *op) override {
        if (op->name.find("halide_xtensa_full_reduce_add") == 0) {
            int native_lanes = get_native_vector_lanes_num(op->args[0].type());
            if (native_lanes > 0) {
                const int total_lanes = op->args[0].type().lanes();
                int split_to = total_lanes / native_lanes;
                Expr arg = mutate(op->args[0]);
                Expr partial_sum;
                for (int ix = 0; ix < split_to; ix++) {
                    Expr sliced_arg = Call::make(arg.type().with_lanes(native_lanes),
                                                 "halide_xtensa_slice_to_native",
                                                 {arg, ix, native_lanes, total_lanes},
                                                 Call::PureExtern);
                    sliced_arg = Call::make(op->type, op->name, {sliced_arg}, op->call_type);
                    if (!partial_sum.defined()) {
                        partial_sum = sliced_arg;
                    } else {
                        partial_sum = Add::make(partial_sum, sliced_arg);
                    }
                }

                return partial_sum;
            }
        }

        if (op->name == "halide_xtensa_widening_load") {
            int native_lanes = get_native_vector_lanes_num(op->type);

            if ((native_lanes > 0) && (2 * native_lanes < op->type.lanes())) {
                const int total_lanes = op->type.lanes();
                int split_to = total_lanes / (2 * native_lanes);
                std::vector<Expr> sliced_loads;

                for (int ix = 0; ix < split_to; ix++) {
                    Expr sliced_load = Call::make(op->type.with_lanes(2 * native_lanes), op->name, {op->args[0], op->args[1] + 2 * native_lanes * ix, op->args[2]}, Call::PureExtern);
                    sliced_loads.push_back(sliced_load);
                }
                return Call::make(op->type,
                                  "halide_xtensa_concat_from_native",
                                  sliced_loads, Call::PureExtern);
            }
        }

        const int total_lanes = op->type.lanes();
        int native_lanes = get_native_vector_lanes_num(op->type);
        std::set<std::string> skip_slicing = {"halide_xtensa_widening_load", "halide_xtensa_interleave_i16",
                                              "halide_xtensa_narrow_i24_with_shift_i16",
                                              // TODO(vksnk): ugly to list them all.
                                              "halide_xtensa_reduce_add_x2_i8",
                                              "halide_xtensa_reduce_add_x2_i16",
                                              "halide_xtensa_reduce_add_x2_i32",
                                              "halide_xtensa_reduce_add_x4_i8",
                                              "halide_xtensa_reduce_add_x4_i16",
                                              "halide_xtensa_reduce_add_x4_i32",
                                              "reinterpret"};
        // For some of the ops, it's better to slice into larger chunks.
        std::map<std::string, int> slicing_multipliers = {
            // There is only interleaved version of this intrinsic, so 2x vectors are required.
            {"halide_xtensa_narrow_i48_with_shift_i32", 2},
            {"halide_xtensa_narrow_i48_with_shift_u32", 2},
            {"halide_xtensa_widen_right_mul_i64", 2},
            {"halide_xtensa_widen_right_mul_u64", 2}};
        int slicing_multiplier = 1;
        if (slicing_multipliers.count(op->name) > 0) {
            slicing_multiplier = slicing_multipliers[op->name];
        }

        if ((native_lanes > 0) && (native_lanes * slicing_multiplier < total_lanes) && (skip_slicing.count(op->name) == 0)) {
            int split_to = op->type.lanes() / (native_lanes * slicing_multiplier);
            vector<Expr> args;
            for (const auto &arg : op->args) {
                args.push_back(mutate(arg));
            }

            std::vector<Expr> concat_args;
            for (int ix = 0; ix < split_to; ix++) {
                std::vector<Expr> sliced_args;
                for (size_t arg_index = 0; arg_index < op->args.size(); arg_index++) {
                    Expr sliced_arg;
                    if (args[arg_index].type().is_scalar()) {
                        sliced_arg = args[arg_index];
                        // dynamic_shuffle is tricky, we can actually slice an index,
                        // but not the actual data vector.
                    } else if ((op->name == "halide_xtensa_dynamic_shuffle") && arg_index == 0) {
                        sliced_arg = args[arg_index];
                    } else {
                        sliced_arg = Call::make(args[arg_index].type().with_lanes(native_lanes * slicing_multiplier),
                                                "halide_xtensa_slice_to_native",
                                                {args[arg_index], ix, native_lanes * slicing_multiplier, total_lanes},
                                                Call::PureExtern);
                    }
                    sliced_args.push_back(sliced_arg);
                }

                Expr r = Call::make(op->type.with_lanes(native_lanes * slicing_multiplier), op->name, sliced_args, op->call_type);
                concat_args.push_back(std::move(r));
            }

            return Call::make(op->type,
                              "halide_xtensa_concat_from_native",
                              concat_args, Call::PureExtern);
        }

        // TODO(vksnk): need to be careful here, because not everything can be
        // padded safely.
        int width_to_extend = get_width_to_extend(op->type);
        bool is_safe_to_pad = true;
        for (const auto &arg : op->args) {
            is_safe_to_pad = is_safe_to_pad && (arg.type().is_scalar() || (op->type.lanes() == arg.type().lanes()));
        }
        std::set<std::string> safe_to_pad = {"halide_xtensa_dynamic_shuffle"};
        is_safe_to_pad = is_safe_to_pad || (safe_to_pad.count(op->name) > 0);
        std::set<std::string> skip_padding = {"halide_xtensa_widening_load"};
        is_safe_to_pad = is_safe_to_pad && (skip_padding.count(op->name) == 0);
        if (width_to_extend > 0 && is_safe_to_pad) {
            vector<Expr> args;
            const int lanes = op->type.lanes();

            for (const auto &arg : op->args) {
                Expr padded_arg;
                if (arg.type().is_scalar()) {
                    padded_arg = arg;
                } else {
                    Expr mutated_arg = mutate(arg);
                    padded_arg = pad(mutated_arg, lanes, width_to_extend);
                }

                args.push_back(padded_arg);
            }

            Expr r = Call::make(op->type.with_lanes(width_to_extend), op->name, args, op->call_type);

            return slice(r, op->type, lanes);
        }

        return IRMutator::visit(op);
    }

    Expr visit(const VectorReduce *op) override {
        // TODO(vksnk): Factor it out.
        Expr (*binop)(Expr, Expr) = nullptr;
        switch (op->op) {
        case VectorReduce::Add:
            binop = Add::make;
            break;
        case VectorReduce::Mul:
            binop = Mul::make;
            break;
        case VectorReduce::Min:
            binop = Min::make;
            break;
        case VectorReduce::Max:
            binop = Max::make;
            break;
        case VectorReduce::And:
            binop = And::make;
            break;
        case VectorReduce::Or:
            binop = Or::make;
            break;
        case VectorReduce::SaturatingAdd:
            binop = saturating_add;
            break;
        }

        int native_lanes = get_native_vector_lanes_num(op->value.type());
        // Only support full reductions for now.
        if (native_lanes > 0 && op->type.is_scalar()) {
            const int total_lanes = op->type.lanes();
            int split_to = op->value.type().lanes() / native_lanes;
            Expr v = mutate(op->value);

            Expr partial_reduction;
            for (int ix = 0; ix < split_to; ix++) {
                Expr sliced_v = Call::make(v.type().with_lanes(native_lanes),
                                           "halide_xtensa_slice_to_native",
                                           {v, ix, native_lanes, total_lanes},
                                           Call::PureExtern);
                sliced_v = VectorReduce::make(op->op, sliced_v, 1);
                if (!partial_reduction.defined()) {
                    partial_reduction = sliced_v;
                } else {
                    partial_reduction = binop(partial_reduction, sliced_v);
                }
            }

            return partial_reduction;
        }

        return IRMutator::visit(op);
    }

public:
    SplitVectorsToNativeSizes(const Target &target) {
        if (target.has_feature(Target::Feature::XtensaQ8)) {
            native_vector_types = {
                {Type(Type::Int, 8, 128)},
                {Type(Type::UInt, 8, 128)},
                {Type(Type::Int, 16, 64)},
                {Type(Type::UInt, 16, 64)},
                {Type(Type::Int, 32, 32)},
                {Type(Type::UInt, 32, 32)},
                {Type(Type::Int, 24, 128)},
                {Type(Type::Int, 48, 64)},
                {Type(Type::Int, 64, 32)},
                {Type(Type::Float, 16, 64)},
                {Type(Type::Float, 32, 32)},
            };
        } else {
            native_vector_types = {
                {Type(Type::Int, 8, 64)},
                {Type(Type::UInt, 8, 64)},
                {Type(Type::Int, 16, 32)},
                {Type(Type::UInt, 16, 32)},
                {Type(Type::Int, 32, 16)},
                {Type(Type::UInt, 32, 16)},
                {Type(Type::Int, 24, 64)},
                {Type(Type::Int, 48, 32)},
                {Type(Type::Int, 64, 16)},
                {Type(Type::Float, 16, 32)},
                {Type(Type::Float, 32, 16)},
            };
        }
    }
};

class SimplifySliceConcat : public IRGraphMutator {
private:
    using IRGraphMutator::visit;

    Expr visit(const Call *op) override {
        if (op->name == "halide_xtensa_concat_from_native") {
            if (op->args.size() == 1) {
                return mutate(op->args[0]);
            }
        }

        if (op->name == "halide_xtensa_slice_from_padded") {
            if (const Broadcast *broadcast = op->args[0].as<Broadcast>()) {
                return Broadcast::make(broadcast->value, op->type.lanes());
            }
            if (const Cast *cast = op->args[0].as<Cast>()) {
                if (const Broadcast *broadcast = cast->value.as<Broadcast>()) {
                    return Broadcast::make(Cast::make(cast->type.with_lanes(broadcast->value.type().lanes()), broadcast->value), op->type.lanes());
                }
            }
        }

        if (op->name == "halide_xtensa_slice_to_native") {
            Expr first_arg = mutate(op->args[0]);
            const Call *maybe_concat_call = first_arg.as<Call>();
            int slice_index = op->args[1].as<IntImm>()->value;
            int native_lanes = op->args[2].as<IntImm>()->value;
            int total_lanes = op->args[3].as<IntImm>()->value;
            if (maybe_concat_call && (maybe_concat_call->name == "halide_xtensa_concat_from_native") && (maybe_concat_call->type.lanes() == total_lanes) && ((int)maybe_concat_call->args.size() == total_lanes / native_lanes)) {
                return maybe_concat_call->args[slice_index];
            }

            if (maybe_concat_call && (maybe_concat_call->name == "halide_xtensa_concat_from_native") && (maybe_concat_call->type.lanes() == total_lanes) && (maybe_concat_call->args[0].type().lanes() % native_lanes == 0)) {
                int concat_group_size = maybe_concat_call->args[0].type().lanes() / native_lanes;
                int new_index = slice_index % concat_group_size;
                int concat_arg_index = slice_index / concat_group_size;

                return Call::make(op->type,
                                  "halide_xtensa_slice_to_native",
                                  {maybe_concat_call->args[concat_arg_index], new_index, native_lanes,
                                   maybe_concat_call->args[concat_arg_index].type().lanes()},
                                  Call::PureExtern);
            }

            const Shuffle *maybe_concat_shuffle = first_arg.as<Shuffle>();
            if (maybe_concat_shuffle && maybe_concat_shuffle->is_concat() && ((int)maybe_concat_shuffle->vectors.size() == total_lanes / native_lanes) && ((int)maybe_concat_shuffle->vectors[slice_index].type().lanes() == native_lanes)) {
                return maybe_concat_shuffle->vectors[slice_index];
            }

            // TODO(vksnk): this looks very similar to above, maybe it's time to move to Shuffle::concat everywhere.
            if (maybe_concat_shuffle && maybe_concat_shuffle->is_concat() && (maybe_concat_shuffle->vectors[0].type().lanes() % native_lanes == 0)) {
                internal_assert(total_lanes == maybe_concat_shuffle->type.lanes());
                int concat_group_size = maybe_concat_shuffle->vectors[0].type().lanes() / native_lanes;
                int new_index = slice_index % concat_group_size;
                int concat_arg_index = slice_index / concat_group_size;

                return Call::make(op->type,
                                  "halide_xtensa_slice_to_native",
                                  {maybe_concat_shuffle->vectors[concat_arg_index], new_index, native_lanes,
                                   maybe_concat_shuffle->vectors[concat_arg_index].type().lanes()},
                                  Call::PureExtern);
            }

            if (first_arg.type().is_bool() && first_arg.type().is_scalar()) {
                return first_arg;
            }

            const Broadcast *maybe_broadcast = first_arg.as<Broadcast>();
            if (maybe_broadcast) {
                return Broadcast::make(maybe_broadcast->value, op->type.lanes());
            }

            return Call::make(op->type, op->name,
                              {first_arg, op->args[1], op->args[2], op->args[3]},
                              Call::PureExtern);
        }

        if (op->name == "halide_xtensa_pad_to_native") {
            Expr first_arg = mutate(op->args[0]);
            const Call *maybe_slice_call = first_arg.as<Call>();
            int lanes_before_padding = op->args[1].as<IntImm>()->value;
            if (maybe_slice_call &&
                (maybe_slice_call->name == "halide_xtensa_slice_from_padded") && (maybe_slice_call->type.lanes() == lanes_before_padding) && (op->type.lanes() == maybe_slice_call->args[0].type().lanes())) {
                return maybe_slice_call->args[0];
            }

            if (maybe_slice_call &&
                (maybe_slice_call->name == "halide_xtensa_slice_from_padded") && (maybe_slice_call->type.lanes() == lanes_before_padding) && (op->type.lanes() > maybe_slice_call->args[0].type().lanes())) {
                return Call::make(op->type,
                                  "halide_xtensa_pad_to_native",
                                  {maybe_slice_call->args[0], op->args[1]},
                                  Call::PureExtern);
            }

            const Shuffle *maybe_shuffle = first_arg.as<Shuffle>();
            if (maybe_shuffle && maybe_shuffle->is_slice() && (maybe_shuffle->slice_begin() == 0) && (maybe_shuffle->slice_stride() == 1) && (maybe_shuffle->vectors.size() == 1) && ((int)maybe_shuffle->indices.size() == lanes_before_padding) && (op->type.lanes() == maybe_shuffle->vectors[0].type().lanes())) {
                return maybe_shuffle->vectors[0];
            }
            const Broadcast *maybe_broadcast = first_arg.as<Broadcast>();
            if (maybe_broadcast) {
                return Broadcast::make(maybe_broadcast->value, op->type.lanes());
            }

            const Ramp *maybe_ramp = first_arg.as<Ramp>();
            if (maybe_ramp) {
                return Ramp::make(maybe_ramp->base, maybe_ramp->stride, op->type.lanes());
            }

            if (first_arg.type().is_bool() && first_arg.type().is_scalar()) {
                return first_arg;
            }

            return Call::make(op->type, op->name,
                              {first_arg, op->args[1]},
                              Call::PureExtern);
        }
        return IRGraphMutator::visit(op);
    }

    Expr visit(const Shuffle *op) override {
        if (op->is_slice() && op->slice_stride() == 1 && op->vectors.size() == 1) {
            Expr mutated = mutate(op->vectors[0]);
            const Call *maybe_call = mutated.as<Call>();
            if (maybe_call && maybe_call->name == "halide_xtensa_concat_from_native") {
                int offset = 0;
                for (int ix = 0; ix < (int)maybe_call->args.size(); ix++) {
                    if (offset == op->slice_begin()) {
                        std::vector<Expr> new_args;
                        int count = 0;
                        while (count < op->type.lanes()) {
                            new_args.push_back(maybe_call->args[ix]);
                            count += maybe_call->args[ix].type().lanes();
                            ix++;
                        }
                        if (count == op->type.lanes()) {
                            return Call::make(op->type,
                                              "halide_xtensa_concat_from_native",
                                              new_args, Call::PureExtern);
                        }
                        break;
                    }
                    offset += maybe_call->args[ix].type().lanes();
                }
            }
        }

        return IRGraphMutator::visit(op);
    }

public:
    SimplifySliceConcat() = default;
};

Stmt match_xtensa_patterns(const Stmt &stmt, const Target &target) {
    const int alignment = target.natural_vector_size<uint8_t>();
    const int lut_size_in_bytes = 2 * target.natural_vector_size<uint8_t>();
    Stmt s = OptimizeShuffles(alignment, lut_size_in_bytes).mutate(stmt);
    s = align_loads(s, alignment, 1);
    // NOTE(vksnk): CSE seemed to break loop carry
    // s = common_subexpression_elimination(s);

    // Use at most 16 vector registers for carrying values.
    // NOTE(vksnk): loop_carry seems to be a little finicky right now
    // but looks like something we'd definitely want to have, so
    // need to figure out where it goes wrong.
    s = loop_carry(s, 16);
    s = simplify(s);
    for (int ix = 0; ix < 10; ix++) {
        s = MatchXtensaPatterns(target).mutate(s);
    }

    // Split to the native vectors sizes.
    s = substitute_in_all_lets(s);
    s = SplitVectorsToNativeSizes(target).mutate(s);
    for (int ix = 0; ix < 3; ix++) {
        s = SimplifySliceConcat().mutate(s);
    }

    // Extra run to replace cast + concat, etc.
    for (int ix = 0; ix < 10; ix++) {
        s = MatchXtensaPatterns(target).mutate(s);
    }
    // NOTE(vksnk): looks like we shouldn't do simplification in the end.
    // s = simplify(common_subexpression_elimination(s));
    s = DualQuadMulMutator().mutate(s);
    s = common_subexpression_elimination(s);

    // debug(0) << s << "\n";
    return s;
}

}  // namespace Internal
}  // namespace Halide
