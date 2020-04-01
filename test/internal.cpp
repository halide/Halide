#include "Associativity.h"
#include "AutoScheduleUtils.h"
#include "Bounds.h"
#include "CPlusPlusMangle.h"
#include "CSE.h"
#include "CodeGen_C.h"
#include "CodeGen_PyTorch.h"
#include "Deinterleave.h"
#include "Generator.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRPrinter.h"
#include "ModulusRemainder.h"
#include "Monotonic.h"
#include "Reduction.h"
#include "Solve.h"
#include "Target.h"
#include "UniquifyVariableNames.h"
#include "Util.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, const char **argv) {
    IRPrinter::test();
    CodeGen_C::test();
    CodeGen_PyTorch::test();
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
    associativity_test();
    generator_test();
    propagate_estimate_test();
    uniquify_variable_names_test();

    return 0;
}
