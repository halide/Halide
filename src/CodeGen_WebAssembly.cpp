#include "CodeGen_WebAssembly.h"

#include "ConciseCasts.h"
#include "IRMatch.h"
#include "IROperator.h"
#include "LLVM_Headers.h"
#include "Util.h"

#include <sstream>

namespace Halide {
namespace Internal {

using namespace Halide::ConciseCasts;
using namespace llvm;
using std::string;
using std::vector;

CodeGen_WebAssembly::CodeGen_WebAssembly(Target t)
    : CodeGen_Posix(t) {
#if !defined(WITH_WEBASSEMBLY)
    user_error << "llvm build not configured with WebAssembly target enabled.\n";
#endif
    user_assert(LLVM_VERSION >= 110) << "Generating WebAssembly is only supported under LLVM 11+.";
    user_assert(llvm_WebAssembly_enabled) << "llvm build not configured with WebAssembly target enabled.\n";
    user_assert(target.bits == 32) << "Only wasm32 is supported.";
}

void CodeGen_WebAssembly::visit(const Cast *op) {
    {
        Halide::Type src = op->value.type();
        Halide::Type dst = op->type;
        if (upgrade_type_for_arithmetic(src) != src ||
            upgrade_type_for_arithmetic(dst) != dst) {
            // Handle casts to and from types for which we don't have native support.
            CodeGen_Posix::visit(op);
            return;
        }
    }

    if (!op->type.is_vector()) {
        // We only have peephole optimizations for vectors in here.
        CodeGen_Posix::visit(op);
        return;
    }

    vector<Expr> matches;

    struct Pattern {
        Target::Feature feature;
        bool wide_op;
        Type type;
        int min_lanes;
        string intrin;
        Expr pattern;
    };

    static Pattern patterns[] = {
        {Target::WasmSimd128, true, Int(8, 16), 0, "llvm.sadd.sat.v16i8", i8_sat(wild_i16x_ + wild_i16x_)},
        {Target::WasmSimd128, true, UInt(8, 16), 0, "llvm.uadd.sat.v16i8", u8_sat(wild_u16x_ + wild_u16x_)},
        {Target::WasmSimd128, true, Int(16, 8), 0, "llvm.sadd.sat.v8i16", i16_sat(wild_i32x_ + wild_i32x_)},
        {Target::WasmSimd128, true, UInt(16, 8), 0, "llvm.uadd.sat.v8i16", u16_sat(wild_u32x_ + wild_u32x_)},
        // N.B. Saturating subtracts are expressed by widening to a *signed* type
        {Target::WasmSimd128, true, Int(8, 16), 0, "llvm.wasm.sub.saturate.signed.v16i8", i8_sat(wild_i16x_ - wild_i16x_)},
        {Target::WasmSimd128, true, UInt(8, 16), 0, "llvm.wasm.sub.saturate.unsigned.v16i8", u8_sat(wild_i16x_ - wild_i16x_)},
        {Target::WasmSimd128, true, Int(16, 8), 0, "llvm.wasm.sub.saturate.signed.v8i16", i16_sat(wild_i32x_ - wild_i32x_)},
        {Target::WasmSimd128, true, UInt(16, 8), 0, "llvm.wasm.sub.saturate.unsigned.v8i16", u16_sat(wild_i32x_ - wild_i32x_)},

        {Target::WasmSimd128, true, UInt(8, 16), 0, "llvm.wasm.avgr.unsigned.v16i8", u8(((wild_u16x_ + wild_u16x_) + 1) / 2)},
        {Target::WasmSimd128, true, UInt(8, 16), 0, "llvm.wasm.avgr.unsigned.v16i8", u8(((wild_u16x_ + wild_u16x_) + 1) >> 1)},
        {Target::WasmSimd128, true, UInt(16, 8), 0, "llvm.wasm.avgr.unsigned.v8i16", u16(((wild_u32x_ + wild_u32x_) + 1) / 2)},
        {Target::WasmSimd128, true, UInt(16, 8), 0, "llvm.wasm.avgr.unsigned.v8i16", u16(((wild_u32x_ + wild_u32x_) + 1) >> 1)},

        // TODO: LLVM should support this directly, but doesn't yet.
        // To make this work, we need to be able to call the intrinsics with two vecs.
        // @abadams sez: "The way I've had to do this in the past is with force-inlined implementations
        // that accept the wider vec, e.g. see packsswbx16 in src/runtime/x86.ll"
        // {Target::WasmSimd128, false, Int(8, 16), 0, "llvm.wasm.narrow.signed.v16i8.v8i16", i8(wild_i16x_)},
        // {Target::WasmSimd128, false, Int(16, 8), 0, "llvm.wasm.narrow.signed.v8i16.v4i32", i16(wild_i32x_)},
        // {Target::WasmSimd128, false, UInt(8, 16), 0, "llvm.wasm.narrow.unsigned.v16i8.v8i16", u8(wild_u16x_)},
        // {Target::WasmSimd128, false, UInt(16, 8), 0, "llvm.wasm.narrow.unsigned.v8i16.v4i32", u16(wild_u32x_)},
    };

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        const Pattern &pattern = patterns[i];

        if (!target.has_feature(pattern.feature)) {
            continue;
        }

        if (op->type.lanes() < pattern.min_lanes) {
            continue;
        }

        if (expr_match(pattern.pattern, op, matches)) {
            bool match = true;
            if (pattern.wide_op) {
                // Try to narrow the matches to the target type.
                for (size_t i = 0; i < matches.size(); i++) {
                    matches[i] = lossless_cast(op->type, matches[i]);
                    if (!matches[i].defined()) {
                        match = false;
                    }
                }
            }
            if (match) {
                value = call_intrin(op->type, pattern.type.lanes(), pattern.intrin, matches);
                return;
            }
        }
    }

    CodeGen_Posix::visit(op);
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
        s << sep << ",+atomics,+bulk-memory";
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

}  // namespace Internal
}  // namespace Halide
