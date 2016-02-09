#include "HexagonOffload.h"
#include "IRMutator.h"
#include "Closure.h"
#include "Param.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

/////////////////////////////////////////////////////////////////////////////
class InjectHexagonRpc : public IRMutator {
    using IRMutator::visit;

public:
    Module device_code;

    InjectHexagonRpc() : device_code("hexagon", Target(Target::HexagonStandalone, Target::Hexagon, 32)) {}

    void visit(const For *loop) {
        if (loop->device_api == DeviceAPI::Hexagon) {
            std::string hex_name = "hex_" + loop->name;

            // Build a closure for the device code.
            // TODO: Should this move the body of the loop to Hexagon,
            // or the loop itself? Currently, this moves the loop itself.
            Closure c(loop);

            // Make an argument list, and generate a function in the device_code module.
            std::vector<Argument> args;
            for (const auto& i : c.buffers) {
                Argument::Kind kind = Argument::InputBuffer;
                if (i.second.write) {
                    kind = Argument::OutputBuffer;
                }
                args.push_back(Argument(i.first, kind, i.second.type, i.second.dimensions));
            }
            for (const auto& i : c.vars) {
                args.push_back(Argument(i.first, Argument::InputScalar, i.second, 0));
            }
            device_code.append(LoweredFunc(hex_name, args, loop, LoweredFunc::External));

            // Generate a call to hexagon_device_run.
            std::vector<Expr> params;
            params.push_back(user_context_value());
            params.push_back(hex_name);
            for (const auto& i : c.buffers) {
                params.push_back(Variable::make(Handle(), i.first + ".buffer"));
            }
            for (const auto& i : c.vars) {
                params.push_back(Variable::make(i.second, i.first));
            }

            stmt = Evaluate::make(Call::make(Int(32), "halide_hexagon_run", params, Call::Extern));
        } else {
            IRMutator::visit(loop);
        }
    }
};

Stmt inject_hexagon_rpc(Stmt s, std::vector<Module>& device_code) {
    InjectHexagonRpc injector;
    s = injector.mutate(s);
    device_code.push_back(injector.device_code);
    return s;
}

}

}
