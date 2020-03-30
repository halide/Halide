#include "Halide.h"
using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    // Test if LICM can lift out the condition correctly
    Stmt s = For::make("x", Expr(0), Expr(10), ForType::Serial, DeviceAPI::Host,
        For::make("y", Expr(0), Expr(10), ForType::Serial, DeviceAPI::Host,
            IfThenElse::make(Var("x"), Evaluate::make(0), Stmt())
        )
    );
    s = loop_invariant_code_motion(s, true /* always_lift */);
    const For *loop = s.as<For>();
    if (loop == nullptr) {
        printf("LICM fails to lift conditions correctly");
        return -1;
    }
    if (loop->body.as<IfThenElse>() == nullptr) {
        printf("LICM fails to lift conditions correctly");
        return -1;
    }
    return 0;
}

