#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    // A bare verbosity matches purely on level, at any location.
    internal_assert(debug_spec_accepts("2", 0, "any.cpp", "any", 1));
    internal_assert(debug_spec_accepts("2", 2, "any.cpp", "any", 1));
    internal_assert(!debug_spec_accepts("2", 3, "any.cpp", "any", 1));

    // Filenames are matched as suffixes, subject to the verbosity bound.
    internal_assert(debug_spec_accepts("4,CodeGen_LLVM.cpp", 4, "src/CodeGen_LLVM.cpp", "f", 10));
    internal_assert(!debug_spec_accepts("4,CodeGen_LLVM.cpp", 4, "src/Simplify.cpp", "f", 10));
    internal_assert(!debug_spec_accepts("4,CodeGen_LLVM.cpp", 5, "src/CodeGen_LLVM.cpp", "f", 10));

    // Line ranges are inclusive on both ends.
    internal_assert(debug_spec_accepts("3,Simplify.cpp:100-180", 3, "src/Simplify.cpp", "f", 100));
    internal_assert(debug_spec_accepts("3,Simplify.cpp:100-180", 3, "src/Simplify.cpp", "f", 180));
    internal_assert(!debug_spec_accepts("3,Simplify.cpp:100-180", 3, "src/Simplify.cpp", "f", 99));
    internal_assert(!debug_spec_accepts("3,Simplify.cpp:100-180", 3, "src/Simplify.cpp", "f", 181));

    // A single line means low == high.
    internal_assert(debug_spec_accepts("3,Simplify.cpp:100", 3, "src/Simplify.cpp", "f", 100));
    internal_assert(!debug_spec_accepts("3,Simplify.cpp:100", 3, "src/Simplify.cpp", "f", 101));

    // Functions are also matched as suffixes.
    internal_assert(debug_spec_accepts("2@visit", 2, "any.cpp", "visit", 1));
    internal_assert(debug_spec_accepts("2@visit", 2, "any.cpp", "IRVisitor::visit", 1));
    internal_assert(!debug_spec_accepts("2@visit", 2, "any.cpp", "mutate", 1));

    // File, line, and function qualifiers combine (all must hold).
    internal_assert(debug_spec_accepts("3,Simplify.cpp:100-180@visit", 3, "Simplify.cpp", "visit", 150));
    internal_assert(!debug_spec_accepts("3,Simplify.cpp:100-180@visit", 3, "Simplify.cpp", "mutate", 150));

    // Rules separated by ';' are OR-ed together.
    internal_assert(debug_spec_accepts("1;4,CodeGen_LLVM.cpp@compile", 1, "whatever.cpp", "g", 5));
    internal_assert(debug_spec_accepts("1;4,CodeGen_LLVM.cpp@compile", 4, "CodeGen_LLVM.cpp", "compile", 5));
    internal_assert(!debug_spec_accepts("1;4,CodeGen_LLVM.cpp@compile", 4, "CodeGen_LLVM.cpp", "other", 5));

    // An empty spec behaves like verbosity 0: only debug(0) prints.
    internal_assert(debug_spec_accepts("", 0, "any.cpp", "f", 1));
    internal_assert(!debug_spec_accepts("", 1, "any.cpp", "f", 1));

    // A malformed rule is skipped (and warns on stderr); with no valid rules,
    // nothing matches. A valid rule alongside it still takes effect.
    internal_assert(!debug_spec_accepts("garbage", 0, "any.cpp", "f", 1));
    internal_assert(debug_spec_accepts("2;garbage", 2, "any.cpp", "f", 1));

    printf("Success!\n");
    return 0;
}
