#include "IR.h"
#include "IRPrinter.h"
#include "CodeGen_X86.h"
#include "CodeGen_C.h"
#include "Func.h"
#include "Simplify.h"
#include "Bounds.h"
#include "Lower.h"
#include "IRMatch.h"
#include "Deinterleave.h"
#include "ModulusRemainder.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, const char **argv) {
    IRPrinter::test();
    CodeGen_X86::test();
    CodeGen_C::test();
    simplify_test();
    bounds_test();
    lower_test();
    Func::test();
    expr_match_test();
    deinterleave_vector_test();
    modulus_remainder_test();
    return 0;
}
