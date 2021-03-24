#include <sstream>

#include "ConciseCasts.h"
#include "CodeGen_Posix.h"
#include "LLVM_Headers.h"
#include "IRMatch.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

using namespace Halide::ConciseCasts;

#if defined(WITH_WEBASSEMBLY)

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

#if LLVM_VERSION >= 130
    {"widening_mul_i8x8", Int(16, 8), "widening_mul", {Int(8, 8), Int(8, 8)}, Target::WasmSimd128},
    {"widening_mul_i8x16", Int(16, 16), "widening_mul", {Int(8, 16), Int(8, 16)}, Target::WasmSimd128},
    {"widening_mul_i16x4", Int(32, 4), "widening_mul", {Int(16, 4), Int(16, 4)}, Target::WasmSimd128},
    {"widening_mul_i16x8", Int(32, 8), "widening_mul", {Int(16, 8), Int(16, 8)}, Target::WasmSimd128},
    {"widening_mul_i32x2", Int(64, 2), "widening_mul", {Int(32, 2), Int(32, 2)}, Target::WasmSimd128},
    {"widening_mul_i32x4", Int(64, 4), "widening_mul", {Int(32, 4), Int(32, 4)}, Target::WasmSimd128},


//    {"llvm.wasm.dot", Int(32, 4), "dot_product", {Int(8, 16), Int(8, 16)}, Target::WasmSimd128},
#endif

    // TODO: LLVM should support this directly, but doesn't yet.
    // To make this work, we need to be able to call the intrinsics with two vecs.
    // @abadams sez: "The way I've had to do this in the past is with force-inlined implementations
    // that accept the wider vec, e.g. see packsswbx16 in src/runtime/x86.ll"
    // {Target::WasmSimd128, false, Int(8, 16), 0, "llvm.wasm.narrow.signed.v16i8.v8i16", i8(wild_i16x_)},
    // {Target::WasmSimd128, false, Int(16, 8), 0, "llvm.wasm.narrow.signed.v8i16.v4i32", i16(wild_i32x_)},
    // {Target::WasmSimd128, false, UInt(8, 16), 0, "llvm.wasm.narrow.unsigned.v16i8.v8i16", u8(wild_u16x_)},
    // {Target::WasmSimd128, false, UInt(16, 8), 0, "llvm.wasm.narrow.unsigned.v8i16.v4i32", u16(wild_u32x_)},
};
// clang-format on

void CodeGen_WebAssembly::init_module() {
    CodeGen_Posix::init_module();

    for (const WasmIntrinsic &i : intrinsic_defs) {
        if (i.feature != Target::FeatureEnd && !target.has_feature(i.feature)) {
            continue;
        }

        Type ret_type = i.ret_type;
        std::vector<Type> arg_types;
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

void CodeGen_WebAssembly::codegen_vector_reduce(const VectorReduce *op, const Expr &init) {
    debug(0) << "codegen_vector_reduce "<<Expr(op)<<"\n";

#if 0 && LLVM_VERSION >= 130
    struct Pattern {
        VectorReduce::Operator reduce_op;
        int factor;
        Expr pattern;
        const char *intrin;
        Target::Feature required_feature;
    };
    // clang-format off
    static const Pattern patterns[] = {
        {VectorReduce::Add, 4, i32(widening_mul(wild_i8x_, wild_i8x_)), "dot_product", Target::WasmSimd128},
    };
    // clang-format on

    int factor = op->value.type().lanes() / op->type.lanes();
    vector<Expr> matches;
    for (const Pattern &p : patterns) {
        if (op->op != p.reduce_op || factor % p.factor != 0) {
            continue;
        }
        if (!target.has_feature(p.required_feature)) {
            continue;
        }
        if (expr_match(p.pattern, op->value, matches)) {
            if (factor != 4) {
                Expr equiv = VectorReduce::make(op->op, op->value, op->value.type().lanes() / 4);
                equiv = VectorReduce::make(op->op, equiv, op->type.lanes());
                codegen_vector_reduce(equiv.as<VectorReduce>(), init);
                return;
            }

            Expr i = init;
            if (!i.defined()) {
                i = make_zero(op->type);
            }
            if (const Shuffle *s = matches[0].as<Shuffle>()) {
                if (s->is_broadcast()) {
                    // LLVM wants the broadcast as the second operand for the broadcasting
                    // variant of udot/sdot.
                    std::swap(matches[0], matches[1]);
                }
            }
            value = call_overloaded_intrin(op->type, p.intrin, {i, matches[0], matches[1]});
            if (value) {
                return;
            }
        }
    }
#if 0
    // TODO: Move this to be patterns? The patterns are pretty trivial, but some
    // of the other logic is tricky.
    const char *intrin = nullptr;
    vector<Expr> intrin_args;
    Expr accumulator = init;
    if (op->op == VectorReduce::Add && factor == 2) {
        Type narrow_type = op->type.narrow().with_lanes(op->value.type().lanes());
        Expr narrow = lossless_cast(narrow_type, op->value);
        if (!narrow.defined() && op->type.is_int()) {
            // We can also safely accumulate from a uint into a
            // wider int, because the addition uses at most one
            // extra bit.
            narrow = lossless_cast(narrow_type.with_code(Type::UInt), op->value);
        }
        if (narrow.defined()) {
            if (init.defined() && target.bits == 32) {
                // On 32-bit, we have an intrinsic for widening add-accumulate.
                intrin = "pairwise_widening_add_accumulate";
                intrin_args = {accumulator, narrow};
                accumulator = Expr();
            } else {
                // On 64-bit, LLVM pattern matches widening add-accumulate if
                // we give it the widening add.
                intrin = "pairwise_widening_add";
                intrin_args = {narrow};
            }
        } else {
            intrin = "pairwise_add";
            intrin_args = {op->value};
        }
    } else if (op->op == VectorReduce::Min && factor == 2) {
        intrin = "pairwise_min";
        intrin_args = {op->value};
    } else if (op->op == VectorReduce::Max && factor == 2) {
        intrin = "pairwise_max";
        intrin_args = {op->value};
    }

    if (intrin) {
        value = call_overloaded_intrin(op->type, intrin, intrin_args);
        if (value) {
            if (accumulator.defined()) {
                // We still have an initial value to take care of
                string n = unique_name('t');
                sym_push(n, value);
                Expr v = Variable::make(accumulator.type(), n);
                switch (op->op) {
                case VectorReduce::Add:
                    accumulator += v;
                    break;
                case VectorReduce::Min:
                    accumulator = min(accumulator, v);
                    break;
                case VectorReduce::Max:
                    accumulator = max(accumulator, v);
                    break;
                default:
                    internal_error << "unreachable";
                }
                codegen(accumulator);
                sym_pop(n);
            }
            return;
        }
    }
#endif
#endif
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
    user_assert(LLVM_VERSION >= 110) << "Generating WebAssembly is only supported under LLVM 11+.";
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
