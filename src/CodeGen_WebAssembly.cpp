#include <functional>
#include <sstream>

#include "CodeGen_Posix.h"
#include "ConciseCasts.h"
#include "IRMatch.h"
#include "IROperator.h"
#include "LLVM_Headers.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

#if defined(WITH_WEBASSEMBLY)

using namespace Halide::ConciseCasts;

namespace {

/** A code generator that emits WebAssembly code from a given Halide stmt. */
class CodeGen_WebAssembly : public CodeGen_Posix {
public:
    CodeGen_WebAssembly(const Target &);

protected:
    using CodeGen_Posix::visit;

    void init_module() override;

    string mcpu() const override;
    string mattrs() const override;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;
    bool use_pic() const override;

    void visit(const Cast *) override;
    void codegen_vector_reduce(const VectorReduce *, const Expr &) override;
};

CodeGen_WebAssembly::CodeGen_WebAssembly(const Target &t)
    : CodeGen_Posix(t) {
}

constexpr int max_intrinsic_args = 4;

struct WasmIntrinsic {
    const char *intrin_name;
    halide_type_t ret_type;
    const char *name;
    halide_type_t arg_types[max_intrinsic_args];
    Target::Feature feature = Target::FeatureEnd;
};

// clang-format off
const WasmIntrinsic intrinsic_defs[] = {
    {"llvm.sadd.sat.v8i16", Int(16, 8), "saturating_add", {Int(16, 8), Int(16, 8)}, Target::WasmSimd128},
    {"llvm.uadd.sat.v8i16", UInt(16, 8), "saturating_add", {UInt(16, 8), UInt(16, 8)}, Target::WasmSimd128},
    {"llvm.sadd.sat.v16i8", Int(8, 16), "saturating_add", {Int(8, 16), Int(8, 16)}, Target::WasmSimd128},
    {"llvm.uadd.sat.v16i8", UInt(8, 16), "saturating_add", {UInt(8, 16), UInt(8, 16)}, Target::WasmSimd128},

    // TODO: Are these really different than the standard llvm.*sub.sat.*?
#if LLVM_VERSION >= 130
    {"llvm.wasm.sub.sat.signed.v16i8", Int(8, 16), "saturating_sub", {Int(8, 16), Int(8, 16)}, Target::WasmSimd128},
    {"llvm.wasm.sub.sat.unsigned.v16i8", UInt(8, 16), "saturating_sub", {UInt(8, 16), UInt(8, 16)}, Target::WasmSimd128},
    {"llvm.wasm.sub.sat.signed.v8i16", Int(16, 8), "saturating_sub", {Int(16, 8), Int(16, 8)}, Target::WasmSimd128},
    {"llvm.wasm.sub.sat.unsigned.v8i16", UInt(16, 8), "saturating_sub", {UInt(16, 8), UInt(16, 8)}, Target::WasmSimd128},
#else
    {"llvm.wasm.sub.saturate.signed.v16i8", Int(8, 16), "saturating_sub", {Int(8, 16), Int(8, 16)}, Target::WasmSimd128},
    {"llvm.wasm.sub.saturate.unsigned.v16i8", UInt(8, 16), "saturating_sub", {UInt(8, 16), UInt(8, 16)}, Target::WasmSimd128},
    {"llvm.wasm.sub.saturate.signed.v8i16", Int(16, 8), "saturating_sub", {Int(16, 8), Int(16, 8)}, Target::WasmSimd128},
    {"llvm.wasm.sub.saturate.unsigned.v8i16", UInt(16, 8), "saturating_sub", {UInt(16, 8), UInt(16, 8)}, Target::WasmSimd128},
#endif

    {"llvm.wasm.avgr.unsigned.v16i8", UInt(8, 16), "rounding_halving_add", {UInt(8, 16), UInt(8, 16)}, Target::WasmSimd128},
    {"llvm.wasm.avgr.unsigned.v8i16", UInt(16, 8), "rounding_halving_add", {UInt(16, 8), UInt(16, 8)}, Target::WasmSimd128},

#if LLVM_VERSION == 130
    {"float_to_double", Float(64, 4), "float_to_double", {Float(32, 4)}, Target::WasmSimd128},
#endif

#if LLVM_VERSION >= 130
    // With some work, some of these could possibly be adapted to work under earlier versions of LLVM.
    {"widening_mul_i8x16", Int(16, 16), "widening_mul", {Int(8, 16), Int(8, 16)}, Target::WasmSimd128},
    {"widening_mul_i16x8", Int(32, 8), "widening_mul", {Int(16, 8), Int(16, 8)}, Target::WasmSimd128},
    {"widening_mul_i32x4", Int(64, 4), "widening_mul", {Int(32, 4), Int(32, 4)}, Target::WasmSimd128},
    {"widening_mul_u8x16", UInt(16, 16), "widening_mul", {UInt(8, 16), UInt(8, 16)}, Target::WasmSimd128},
    {"widening_mul_u16x8", UInt(32, 8), "widening_mul", {UInt(16, 8), UInt(16, 8)}, Target::WasmSimd128},
    {"widening_mul_u32x4", UInt(64, 4), "widening_mul", {UInt(32, 4), UInt(32, 4)}, Target::WasmSimd128},

    {"llvm.wasm.extadd.pairwise.signed.v8i16", Int(16, 8), "pairwise_widening_add", {Int(8, 16)}, Target::WasmSimd128},
    {"llvm.wasm.extadd.pairwise.unsigned.v8i16", UInt(16, 8), "pairwise_widening_add", {UInt(8, 16)}, Target::WasmSimd128},
    {"llvm.wasm.extadd.pairwise.signed.v4i32", Int(32, 4), "pairwise_widening_add", {Int(16, 8)}, Target::WasmSimd128},
    {"llvm.wasm.extadd.pairwise.unsigned.v4i32", UInt(32, 4), "pairwise_widening_add", {UInt(16, 8)}, Target::WasmSimd128},
    // There isn't an op for u8x16 -> i16x8, but we can just the u8x16 -> u16x8 op and treat the result as i16x8,
    // since the result will be the same for our purposes here
    {"llvm.wasm.extadd.pairwise.unsigned.v8i16", Int(16, 8), "pairwise_widening_add", {UInt(8, 16)}, Target::WasmSimd128},
    {"llvm.wasm.extadd.pairwise.unsigned.v4i32", Int(32, 4), "pairwise_widening_add", {UInt(16, 8)}, Target::WasmSimd128},

    // Basically like ARM's SQRDMULH
    {"llvm.wasm.q15mulr.sat.signed", Int(16, 8), "q15mulr_sat_s", {Int(16, 8), Int(16, 8)}, Target::WasmSimd128},

    // Note that the inputs are *always* treated as signed, regardless of the output
    {"saturating_narrow_i16x16_to_i8x16", Int(8, 16), "saturating_narrow", {Int(16, 16)}, Target::WasmSimd128},
    {"saturating_narrow_i16x16_to_u8x16", UInt(8, 16), "saturating_narrow", {Int(16, 16)}, Target::WasmSimd128},
    {"saturating_narrow_i32x8_to_i16x8", Int(16, 8), "saturating_narrow", {Int(32, 8)}, Target::WasmSimd128},
    {"saturating_narrow_i32x8_to_u16x8", UInt(16, 8), "saturating_narrow", {Int(32, 8)}, Target::WasmSimd128},

    {"llvm.wasm.dot", Int(32, 4), "dot_product", {Int(16, 8), Int(16, 8)}, Target::WasmSimd128},

    // TODO: LLVM should be able to handle this on its own, but doesn't at top-of-tree as of Jan 2022;
    // if/when https://github.com/llvm/llvm-project/issues/53278 gets addressed, it may be possible to remove
    // these.
    {"extend_i8x16_to_i16x8", Int(16, 16), "widen_integer", {Int(8, 16)}, Target::WasmSimd128},
    {"extend_u8x16_to_u16x8", UInt(16, 16), "widen_integer", {UInt(8, 16)}, Target::WasmSimd128},
    {"extend_i16x8_to_i32x8", Int(32, 8), "widen_integer", {Int(16, 8)}, Target::WasmSimd128},
    {"extend_u16x8_to_u32x8", UInt(32, 8), "widen_integer", {UInt(16, 8)}, Target::WasmSimd128},
    {"extend_i32x4_to_i64x4", Int(64, 4), "widen_integer", {Int(32, 4)}, Target::WasmSimd128},
    {"extend_u32x4_to_u64x4", UInt(64, 4), "widen_integer", {UInt(32, 4)}, Target::WasmSimd128},
#endif
};
// clang-format on

void CodeGen_WebAssembly::init_module() {
    CodeGen_Posix::init_module();

    for (const WasmIntrinsic &i : intrinsic_defs) {
        if (i.feature != Target::FeatureEnd && !target.has_feature(i.feature)) {
            continue;
        }

        Type ret_type = i.ret_type;
        vector<Type> arg_types;
        arg_types.reserve(max_intrinsic_args);
        for (halide_type_t i : i.arg_types) {
            if (i.bits == 0) {
                break;
            }
            arg_types.emplace_back(i);
        }

        auto *fn = declare_intrin_overload(i.name, ret_type, i.intrin_name, std::move(arg_types));
        fn->addFnAttr(llvm::Attribute::ReadNone);
        fn->addFnAttr(llvm::Attribute::NoUnwind);
    }
}

void CodeGen_WebAssembly::visit(const Cast *op) {
#if LLVM_VERSION >= 130
    struct Pattern {
        std::string intrin;  ///< Name of the intrinsic
        Expr pattern;        ///< The pattern to match against
        Target::Feature required_feature;
    };

    // clang-format off
    static const Pattern patterns[] = {
        {"q15mulr_sat_s", i16_sat(rounding_shift_right(widening_mul(wild_i16x_, wild_i16x_), u16(15))), Target::WasmSimd128},
        {"saturating_narrow", i8_sat(wild_i16x_), Target::WasmSimd128},
        {"saturating_narrow", u8_sat(wild_i16x_), Target::WasmSimd128},
        {"saturating_narrow", i16_sat(wild_i32x_), Target::WasmSimd128},
        {"saturating_narrow", u16_sat(wild_i32x_), Target::WasmSimd128},
        {"int_to_double", f64(wild_i32x_), Target::WasmSimd128},
        {"int_to_double", f64(wild_u32x_), Target::WasmSimd128},
#if LLVM_VERSION == 130
        {"float_to_double", f64(wild_f32x_), Target::WasmSimd128},
#endif
        {"widen_integer", i16(wild_i8x_), Target::WasmSimd128},
        {"widen_integer", u16(wild_u8x_), Target::WasmSimd128},
        {"widen_integer", i32(wild_i16x_), Target::WasmSimd128},
        {"widen_integer", u32(wild_u16x_), Target::WasmSimd128},
        {"widen_integer", i64(wild_i32x_), Target::WasmSimd128},
        {"widen_integer", u64(wild_u32x_), Target::WasmSimd128},
    };
    // clang-format on

    if (op->type.is_vector()) {
        std::vector<Expr> matches;
        for (const Pattern &p : patterns) {
            if (!target.has_feature(p.required_feature)) {
                continue;
            }
            if (expr_match(p.pattern, op, matches)) {
                value = call_overloaded_intrin(op->type, p.intrin, matches);
                if (value) {
                    return;
                }
            }
        }
    }
#endif  // LLVM_VERSION >= 130

    CodeGen_Posix::visit(op);
}

void CodeGen_WebAssembly::codegen_vector_reduce(const VectorReduce *op, const Expr &init) {
#if LLVM_VERSION >= 130
    struct Pattern {
        VectorReduce::Operator reduce_op;
        int factor;
        Expr pattern;
        const char *intrin;
        Target::Feature required_feature;
    };
    // clang-format off
    static const Pattern patterns[] = {
        {VectorReduce::Add, 2, i16(wild_i8x_), "pairwise_widening_add", Target::WasmSimd128},
        {VectorReduce::Add, 2, u16(wild_u8x_), "pairwise_widening_add", Target::WasmSimd128},
        {VectorReduce::Add, 2, i16(wild_u8x_), "pairwise_widening_add", Target::WasmSimd128},

        {VectorReduce::Add, 2, i32(wild_i16x_), "pairwise_widening_add", Target::WasmSimd128},
        {VectorReduce::Add, 2, u32(wild_u16x_), "pairwise_widening_add", Target::WasmSimd128},
        {VectorReduce::Add, 2, i32(wild_u16x_), "pairwise_widening_add", Target::WasmSimd128},

        {VectorReduce::Add, 2, i32(widening_mul(wild_i16x_, wild_i16x_)), "dot_product", Target::WasmSimd128},
    };
    // clang-format on

    // Other values will be added soon, so this switch isn't actually pointless
    using ValuePtr = llvm::Value *;
    std::function<ValuePtr(ValuePtr, ValuePtr)> binop = nullptr;
    switch (op->op) {
    case VectorReduce::Add:
        binop = [this](ValuePtr x, ValuePtr y) -> ValuePtr { return this->builder->CreateAdd(x, y); };
        break;
    default:
        break;
    }

    const int factor = op->value.type().lanes() / op->type.lanes();
    vector<Expr> matches;
    for (const Pattern &p : patterns) {
        if (op->op != p.reduce_op || (factor % p.factor) != 0) {
            continue;
        }
        if (!target.has_feature(p.required_feature)) {
            continue;
        }
        if (expr_match(p.pattern, op->value, matches)) {
            if (factor != p.factor) {
                Expr equiv = VectorReduce::make(op->op, op->value, op->value.type().lanes() / p.factor);
                equiv = VectorReduce::make(op->op, equiv, op->type.lanes());
                codegen_vector_reduce(equiv.as<VectorReduce>(), init);
                return;
            }

            if (const Shuffle *s = matches[0].as<Shuffle>()) {
                if (s->is_broadcast() && matches.size() == 2) {
                    // LLVM wants the broadcast as the second operand for the broadcasting
                    // variant of udot/sdot.
                    std::swap(matches[0], matches[1]);
                }
            }
            value = call_overloaded_intrin(op->type, p.intrin, matches);
            if (value) {
                if (init.defined()) {
                    internal_assert(binop != nullptr) << "unsupported op";
                    ValuePtr x = value;
                    ValuePtr y = codegen(init);
                    value = binop(x, y);
                }
                return;
            }
        }
    }
#endif  // LLVM_VERSION >= 130

    CodeGen_Posix::codegen_vector_reduce(op, init);
}

string CodeGen_WebAssembly::mcpu() const {
    return "";
}

string CodeGen_WebAssembly::mattrs() const {
    std::ostringstream s;
    string sep;

    if (target.has_feature(Target::WasmSignExt)) {
        s << sep << "+sign-ext";
        sep = ",";
    }

    if (target.has_feature(Target::WasmSimd128)) {
        s << sep << "+simd128";
        sep = ",";
    }

    if (target.has_feature(Target::WasmSatFloatToInt)) {
        s << sep << "+nontrapping-fptoint";
        sep = ",";
    }

    if (target.has_feature(Target::WasmThreads)) {
        // "WasmThreads" doesn't directly affect LLVM codegen,
        // but it does end up requiring atomics, so be sure to enable them.
        s << sep << ",+atomics";
        sep = ",";
    }

    if (target.has_feature(Target::WasmBulkMemory)) {
        s << sep << "+bulk-memory";
        sep = ",";
    }

    user_assert(target.os == Target::WebAssemblyRuntime)
        << "wasmrt is the only supported 'os' for WebAssembly at this time.";

    return s.str();
}

bool CodeGen_WebAssembly::use_soft_float_abi() const {
    return false;
}

bool CodeGen_WebAssembly::use_pic() const {
    return false;
}

int CodeGen_WebAssembly::native_vector_bits() const {
    return 128;
}

}  // namespace

std::unique_ptr<CodeGen_Posix> new_CodeGen_WebAssembly(const Target &target) {
    user_assert(target.bits == 32) << "Only wasm32 is supported.";
    return std::make_unique<CodeGen_WebAssembly>(target);
}

#else  // WITH_WEBASSEMBLY

std::unique_ptr<CodeGen_Posix> new_CodeGen_WebAssembly(const Target &target) {
    user_error << "WebAssembly not enabled for this build of Halide.\n";
    return nullptr;
}

#endif  // WITH_WEBASSEMBLY

}  // namespace Internal
}  // namespace Halide
