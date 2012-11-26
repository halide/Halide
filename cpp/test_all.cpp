#include "IR.h"
#include "IRPrinter.h"
#include "CodeGen_X86.h"

using namespace HalideInternal;

int main(int argc, const char **argv) {
    IRPrinter::test();
    CodeGen_X86::test();
    return 0;
}
