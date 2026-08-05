// A tiny program that constructs Halide IR and other internal data structures
// with known printed forms, then stops at a stable breakpoint. It exists so the
// debugger-binding tests (run_debugger_test.py) can attach LLDB or GDB, load the
// helpers in tools/lldbhalide.py / tools/gdbhalide.py, and check that each value
// pretty-prints as expected.
//
// Break on the symbol `halide_debugger_test_breakpoint` (robust across
// compilers and source edits); the values to inspect are its parameters, so
// they are live in frame 0 with no need to walk the stack.

#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

// The debugger pretty-printers call Halide::Internal::debug_string() in the
// inferior, which the debugger's expression evaluator can only compile if
// libHalide carries debug info for it (a RelWithDebInfo or Debug build). When it
// does not (e.g. a Release build) the test harness detects that and skips.

// Keep this out-of-line and side-effecting enough that it is never elided.
extern "C" void halide_debugger_test_breakpoint(const Expr &e, const Stmt &s,
                                                const Type &t, const Target &target,
                                                const Buffer<int> &buf) {
    volatile int keep_alive = 0;
    (void)keep_alive;
    (void)e;
    (void)s;
    (void)t;
    (void)target;
    (void)buf;
}

int main() {
    Var x("x"), y("y");

    // Expected debug_string: "max(min(x + y, 100), 20)"
    Expr e = max(min(x + y, 100), 20);

    // A statement, printed in summary form by debug_string(Stmt).
    Stmt s = ProducerConsumer::make_produce("myfunc", Evaluate::make(e));

    // Expected debug_string: "int32"
    Type t = Int(32);

    Target target = get_host_target();

    Buffer<int> buf(4, 4, "myimg");

    halide_debugger_test_breakpoint(e, s, t, target, buf);
    return 0;
}
