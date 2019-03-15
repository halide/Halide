#include "CodeGen_WebAssembly.h"

#include "ConciseCasts.h"
#include "IRMatch.h"
#include "IROperator.h"
#include "LLVM_Headers.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using namespace Halide::ConciseCasts;
using namespace llvm;
using std::string;
using std::vector;

/*
    TODO:
        - wasm only supports an i8x16 shuffle directly; we should sniff our Shuffle
          nodes for (eg) i16x8 and synthesize the right thing
*/

CodeGen_WebAssembly::CodeGen_WebAssembly(Target t) : CodeGen_Posix(t) {
    #if !(WITH_WEBASSEMBLY)
    user_error << "llvm build not configured with WebAssembly target enabled.\n";
    #endif
    user_assert(llvm_WebAssembly_enabled) << "llvm build not configured with WebAssembly target enabled.\n";
    user_assert(target.bits == 32) << "Only wasm32 is supported.";
    #if LLVM_VERSION >= 90
    #else
    user_error << "WebAssembly output is only support in LLVM 9.0+";
    #endif
}

void CodeGen_WebAssembly::visit(const Cast *op) {
    if (!op->type.is_vector()) {
        // We only have peephole optimizations for vectors in here.
        CodeGen_Posix::visit(op);
        return;
    }

    vector<Expr> matches;

    struct Pattern {
        Target::Feature feature;
        bool            wide_op;
        Type            type;
        int             min_lanes;
        string          intrin;
        Expr            pattern;
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
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
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
    // We believe support for this is wide enough as of early 2019
    // to simply enable it by default, rather than hide it behind a Feature
    string s = "+sign-ext";

    // TODO: not ready to enable by default
    // s += ",+bulk-memory";

    if (target.has_feature(Target::WasmSimd128)) {
        s += ",+simd128";
    }

    user_assert(target.os == Target::WebAssemblyRuntime)
        << "wasmrt is the only supported 'os' for WebAssembly at this time.";

    // TODO: Emscripten doesn't seem to be able to validate wasm that contains this yet.
    // We could only generate for JIT mode (where we know we can enable it), but that
    // would mean the execution model for JIT vs AOT could be slightly different,
    // so leave it out entirely until we can do it uniformly.
    // if (target.has_feature(Target::JIT)) {
    //     s += ",+nontrapping-fptoint";
    // }

    return s;
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

}}
