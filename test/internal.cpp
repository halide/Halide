#include "IR.h"
#include "IRPrinter.h"
#include "CodeGen_X86.h"
#include "CodeGen_C.h"
#include "CPlusPlusMangle.h"
#include "Func.h"
#include "Bounds.h"
#include "IRMatch.h"
#include "Deinterleave.h"
#include "ModulusRemainder.h"
#include "CSE.h"
#include "IREquality.h"
#include "Solve.h"
#include "Monotonic.h"
#include "Reduction.h"
#include "Interval.h"
#include "Associativity.h"
#include "Generator.h"
#include "AutoScheduleUtils.h"
#include "CopyElision.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, const char **argv) {
    /*IRPrinter::test();
    CodeGen_C::test();
    ir_equality_test();
    bounds_test();
    expr_match_test();
    deinterleave_vector_test();
    modulus_remainder_test();
    cse_test();
    solve_test();
    target_test();
    cplusplus_mangle_test();
    is_monotonic_test();
    split_predicate_test();
    interval_test();
    associativity_test();
    generator_test();
    propagate_estimate_test();*/
    copy_elision_test();

    return 0;
}
