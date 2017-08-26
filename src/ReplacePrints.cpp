#include <algorithm>

#include "IRMutator.h"
#include "ReplacePrints.h"

namespace Halide {
namespace Internal {

namespace {

class ReplacePrint : public IRMutator {

    const Target &target;
    bool in_hexagon;
    bool in_vectorized;
    using IRMutator::visit;

    void visit(const Call *op) {
        if(op->name == "halide_print" && in_hexagon && in_vectorized) {
            debug(1) << "Found print_halide for vectorized schedule for Hexagon: " << op->name << "\n";
            expr = Call::make(Int(32), "halide_vprint", op->args, Internal::Call::Extern);
        }
        else {
            debug(1) << "No halide_print found....Continue" << "\n";
            IRMutator::visit(op);
        }
    }
  
    void visit(const For *for_loop) {
        bool old_in_hexagon = in_hexagon;
        if (for_loop->device_api == DeviceAPI::Hexagon) {
            in_hexagon = true;
        }

        if (for_loop->for_type == ForType::Vectorized) {
            in_vectorized = true;
        }

        IRMutator::visit(for_loop);

        if (for_loop->device_api == DeviceAPI::Hexagon) {
            in_hexagon = old_in_hexagon;
        }
    }

public:
    ReplacePrint(const Target &t) : target(t), in_hexagon(false), in_vectorized(false) {}    

};

} // Anonymous namespace

Stmt replace_prints(Stmt s, const Target &t) {
    return ReplacePrint(t).mutate(s);
}

}
}
