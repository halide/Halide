#include "IRGraphCXXPrinter.h"

#include "Expr.h"
#include "IR.h"
#include "IREquality.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

void IRGraphCXXPrinter::test() {
    // This:
    Expr e = Select::make(Mod::make(Ramp::make(10, 314, 8), Broadcast::make(10, 8)) < Variable::make(Int(32), "p"), Broadcast::make(40, 8) + Ramp::make(4, 8, 8), VectorReduce::make(VectorReduce::Add, Ramp::make(0, 1, 16), 8));

    // Printed by:
    IRGraphCXXPrinter p(std::cout);
    p.print(e);

    // Prints:
    Expr expr_0 = IntImm::make(Type(Type::Int, 32, 1), 10);
    Expr expr_1 = IntImm::make(Type(Type::Int, 32, 1), 314);
    Expr expr_2 = Ramp::make(expr_0, expr_1, 8);
    Expr expr_3 = IntImm::make(Type(Type::Int, 32, 1), 10);
    Expr expr_4 = Broadcast::make(expr_3, 8);
    Expr expr_5 = Mod::make(expr_2, expr_4);
    Expr expr_6 = Variable::make(Type(Type::Int, 32, 1), "p");
    Expr expr_7 = Broadcast::make(expr_6, 8);
    Expr expr_8 = LT::make(expr_5, expr_7);
    Expr expr_9 = IntImm::make(Type(Type::Int, 32, 1), 40);
    Expr expr_10 = Broadcast::make(expr_9, 8);
    Expr expr_11 = IntImm::make(Type(Type::Int, 32, 1), 4);
    Expr expr_12 = IntImm::make(Type(Type::Int, 32, 1), 8);
    Expr expr_13 = Ramp::make(expr_11, expr_12, 8);
    Expr expr_14 = Add::make(expr_10, expr_13);
    Expr expr_15 = IntImm::make(Type(Type::Int, 32, 1), 0);
    Expr expr_16 = IntImm::make(Type(Type::Int, 32, 1), 1);
    Expr expr_17 = Ramp::make(expr_15, expr_16, 16);
    Expr expr_18 = VectorReduce::make(VectorReduce::Add, expr_17, 8);
    Expr expr_19 = Select::make(expr_8, expr_14, expr_18);

    // Now let's see if it matches:
    internal_assert(equal(expr_19, e)) << "Expressions don't match:\n\n"
                                       << e << "\n\n"
                                        << expr_19 << "\n";

    // Here is a bad typo for Alex who likes progamming. Above is a badly intented line. Two typos?
}
}  // namespace Internal
}  // namespace Halide
