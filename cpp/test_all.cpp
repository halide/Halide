#include "IR.h"
#include "IRPrinter.h"
#include "CodeGen_X86.h"
#include "CodeGen_C.h"
#include "Func.h"

using namespace HalideInternal;

int main(int argc, const char **argv) {
    IRPrinter::test();
    CodeGen_X86::test();
    CodeGen_C::test();
    test_lowering();
    return 0;
}
