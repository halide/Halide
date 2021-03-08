#include "CodeGen_Posix.h"

#include <sstream>

namespace Halide {
namespace Internal {

using std::string;

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
    {"llvm.wasm.sub.saturate.signed.v16i8", Int(8, 16), "saturating_sub", {Int(8, 16), Int(8, 16)}, Target::WasmSimd128},
    {"llvm.wasm.sub.saturate.unsigned.v16i8", UInt(8, 16), "saturating_sub", {UInt(8, 16), UInt(8, 16)}, Target::WasmSimd128},
    {"llvm.wasm.sub.saturate.signed.v8i16", Int(16, 8), "saturating_sub", {Int(16, 8), Int(16, 8)}, Target::WasmSimd128},
    {"llvm.wasm.sub.saturate.unsigned.v8i16", UInt(16, 8), "saturating_sub", {UInt(16, 8), UInt(16, 8)}, Target::WasmSimd128},

    {"llvm.wasm.avgr.unsigned.v16i8", UInt(8, 16), "rounding_halving_add", {UInt(8, 16), UInt(8, 16)}, Target::WasmSimd128},
    {"llvm.wasm.avgr.unsigned.v8i16", UInt(16, 8), "rounding_halving_add", {UInt(16, 8), UInt(16, 8)}, Target::WasmSimd128},

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

        auto fn = declare_intrin_overload(i.name, ret_type, i.intrin_name, std::move(arg_types));
        fn->addFnAttr(llvm::Attribute::AttrKind::ReadNone);
        fn->addFnAttr(llvm::Attribute::AttrKind::NoUnwind);
    }
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
