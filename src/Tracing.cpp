#include "Tracing.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Substitute.h"
#include "Debug.h"
#include "Lower.h"
#include "runtime/HalideRuntime.h"

namespace Halide {
namespace Internal {

int tracing_level() {
    char *trace = getenv("HL_TRACE");
    return trace ? atoi(trace) : 0;
}

using std::vector;
using std::map;
using std::string;

class InjectTracing : public IRMutator {
public:
    const map<string, Function> &env;
    Function output;
    int global_level;
    InjectTracing(const map<string, Function> &e,
                  Function o) : env(e),
                                output(o),
                                global_level(tracing_level()) {}

private:
    using IRMutator::visit;

    void visit(const Call *op) {
        IRMutator::visit(op);
        op = expr.as<Call>();
        assert(op);

        if (op->call_type != Call::Halide) return;

        Function f = op->func;
        bool inlined = !f.same_as(output) && f.schedule().compute_level.is_inline();

        if (f.is_tracing_loads() || (global_level > 2 && !inlined)) {

            // Wrap the load in a call to trace_load
            vector<Expr> args;
            args.push_back(f.name());
            args.push_back(halide_trace_load);
            args.push_back(op->value_index);
            args.push_back(op);
            args.insert(args.end(), op->args.begin(), op->args.end());

            expr = Call::make(op->type, Call::trace, args, Call::Intrinsic);
        }
    }

    void visit(const Provide *op) {
        IRMutator::visit(op);
        op = stmt.as<Provide>();
        assert(op);

        map<string, Function>::const_iterator iter = env.find(op->name);
        if (iter == env.end()) return;
        Function f = iter->second;
        bool inlined = !f.same_as(output) && f.schedule().compute_level.is_inline();

        if (f.is_tracing_stores() || (global_level > 1 && !inlined)) {
            // Wrap each expr in a tracing call

            vector<Expr> new_values(op->values.size());

            for (size_t i = 0; i < new_values.size(); i++) {
                Expr old = op->values[i];
                vector<Expr> args;
                args.push_back(f.name());
                args.push_back(halide_trace_store);
                args.push_back((int)i);
                args.push_back(old);
                args.insert(args.end(), op->args.begin(), op->args.end());
                new_values[i] = Call::make(old.type(), Call::trace, args, Call::Intrinsic);
            }

            stmt = Provide::make(op->name, new_values, op->args);
        }
    }

    void visit(const Realize *op) {
        IRMutator::visit(op);
        op = stmt.as<Realize>();
        assert(op);

        map<string, Function>::const_iterator iter = env.find(op->name);
        if (iter == env.end()) return;
        Function f = iter->second;
        if (f.is_tracing_realizations() || global_level > 0) {

            // Throw a tracing call before and after the realize body
            vector<Expr> args;
            args.push_back(op->name);
            args.push_back(halide_trace_begin_realization); // event type for begin realization

            args.push_back(0); // value index
            args.push_back(0); // value

            for (size_t i = 0; i < op->bounds.size(); i++) {
                args.push_back(op->bounds[i].min);
                args.push_back(op->bounds[i].extent);
            }

            Expr call_before = Call::make(UInt(1), Call::trace, args, Call::Intrinsic);
            args[1] = halide_trace_end_realization;
            Expr call_after = Call::make(UInt(1), Call::trace, args, Call::Intrinsic);
            Stmt new_body = op->body;
            new_body = Block::make(Evaluate::make(call_before), new_body);
            new_body = Block::make(new_body, Evaluate::make(call_after));
            stmt = Realize::make(op->name, op->types, op->bounds, new_body);
        }
    }

    void visit(const Pipeline *op) {
        IRMutator::visit(op);
        op = stmt.as<Pipeline>();
        assert(op);

        map<string, Function>::const_iterator iter = env.find(op->name);
        if (iter == env.end()) return;
        Function f = iter->second;
        if (f.is_tracing_realizations() || global_level > 0) {
            // Throw a tracing call around each pipeline event
            vector<Expr> args;
            args.push_back(op->name);
            args.push_back(halide_trace_produce); // event type for begin realization
            args.push_back(0); // value index
            args.push_back(0); // value

            for (int i = 0; i < f.dimensions(); i++) {
                Expr min = Variable::make(Int(32), f.name() + "." + f.args()[i] + ".min_produced");
                Expr extent = Variable::make(Int(32), f.name() + "." + f.args()[i] + ".extent_produced");
                args.push_back(min);
                args.push_back(extent);
            }

            Expr call = Call::make(UInt(1), Call::trace, args, Call::Intrinsic);
            Stmt new_produce = Block::make(Evaluate::make(call), op->produce);

            Stmt new_update;
            if (op->update.defined()) {
                args[1] = halide_trace_update;
                call = Call::make(UInt(1), Call::trace, args, Call::Intrinsic);
                new_update = Block::make(Evaluate::make(call), op->update);
            }

            args[1] = halide_trace_consume;
            call = Call::make(UInt(1), Call::trace, args, Call::Intrinsic);
            Stmt new_consume = Block::make(Evaluate::make(call), op->consume);

            args[1] = halide_trace_end_consume;
            call = Call::make(UInt(1), Call::trace, args, Call::Intrinsic);
            new_consume = Block::make(new_consume, Evaluate::make(call));

            stmt = Pipeline::make(op->name, new_produce, new_update, new_consume);
        }
    }
};

Stmt inject_tracing(Stmt s, const map<string, Function> &env, Function output) {
    InjectTracing tracing(env, output);
    s = tracing.mutate(s);
    return s;
}

}
}
