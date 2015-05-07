#include "Tracing.h"
#include "IRMutator.h"
#include "IROperator.h"
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
    int global_level;
    InjectTracing(const map<string, Function> &e)
        : env(e),
          global_level(tracing_level()) {}

private:
    using IRMutator::visit;

    void visit(const Call *op) {

        // Calls inside of an address_of don't count, but we want to
        // visit the args of the inner call.
        if (op->call_type == Call::Intrinsic && op->name == Call::address_of) {
            internal_assert(op->args.size() == 1);
            const Call *c = op->args[0].as<Call>();
            const Load *l = op->args[0].as<Load>();

            internal_assert(c || l);

            std::vector<Expr> args;
            if (c) {
                args = c->args;
            } else {
                args.push_back(l->index);
            }

            bool unchanged = true;
            vector<Expr> new_args(args.size());
            for (size_t i = 0; i < args.size(); i++) {
                new_args[i] = mutate(args[i]);
                unchanged = unchanged && (new_args[i].same_as(args[i]));
            }

            if (unchanged) {
                expr = op;
                return;
            } else {
                Expr inner;
                if (c) {
                    inner = Call::make(c->type, c->name, new_args, c->call_type,
                                       c->func, c->value_index, c->image, c->param);
                } else {
                    Expr inner = Load::make(l->type, l->name, new_args[0], l->image, l->param);
                }
                expr = Call::make(Handle(), Call::address_of, vec(inner), Call::Intrinsic);
                return;
            }
        }



        IRMutator::visit(op);
        op = expr.as<Call>();
        internal_assert(op);

        bool trace_it = false;
        Expr trace_parent;
        if (op->call_type == Call::Halide) {
            Function f = op->func;
            bool inlined = f.schedule().compute_level().is_inline();
            trace_it = f.is_tracing_loads() || (global_level > 2 && !inlined);
            trace_parent = Variable::make(Int(32), op->name + ".trace_id");
        } else if (op->call_type == Call::Image) {
            trace_it = global_level > 2;
            trace_parent = -1;
        }

        if (trace_it) {
            // Wrap the load in a call to trace_load
            vector<Expr> args;
            args.push_back(op->name);
            args.push_back(halide_trace_load);
            args.push_back(trace_parent);
            args.push_back(op->value_index);
            args.push_back(op);
            args.insert(args.end(), op->args.begin(), op->args.end());

            expr = Call::make(op->type, Call::trace_expr, args, Call::Intrinsic);
        }

    }

    void visit(const Provide *op) {
        IRMutator::visit(op);
        op = stmt.as<Provide>();
        internal_assert(op);

        map<string, Function>::const_iterator iter = env.find(op->name);
        if (iter == env.end()) return;
        Function f = iter->second;
        bool inlined = f.schedule().compute_level().is_inline();

        if (f.is_tracing_stores() || (global_level > 1 && !inlined)) {
            // Wrap each expr in a tracing call

            const vector<Expr> &values = op->values;
            vector<Expr> traces(op->values.size());

            for (size_t i = 0; i < values.size(); i++) {
                vector<Expr> args;
                args.push_back(f.name());
                args.push_back(halide_trace_store);
                args.push_back(Variable::make(Int(32), op->name + ".trace_id"));
                args.push_back((int)i);
                args.push_back(values[i]);
                args.insert(args.end(), op->args.begin(), op->args.end());
                traces[i] = Call::make(values[i].type(), Call::trace_expr, args, Call::Intrinsic);
            }

            stmt = Provide::make(op->name, traces, op->args);
        }
    }

    void visit(const Realize *op) {
        IRMutator::visit(op);
        op = stmt.as<Realize>();
        internal_assert(op);

        map<string, Function>::const_iterator iter = env.find(op->name);
        if (iter == env.end()) return;
        Function f = iter->second;
        if (f.is_tracing_realizations() || global_level > 0) {

            // Throw a tracing call before and after the realize body
            vector<Expr> args;
            args.push_back(op->name);
            args.push_back(halide_trace_begin_realization); // event type for begin realization
            args.push_back(0); // realization id
            args.push_back(0); // value index
            args.push_back(0); // value

            for (size_t i = 0; i < op->bounds.size(); i++) {
                args.push_back(op->bounds[i].min);
                args.push_back(op->bounds[i].extent);
            }

            // Begin realization returns a unique token to pass to further trace calls affecting this buffer.

            Expr call_before = Call::make(Int(32), Call::trace, args, Call::Intrinsic);
            args[1] = halide_trace_end_realization;
            args[2] = Variable::make(Int(32), op->name + ".trace_id");
            Expr call_after = Call::make(Int(32), Call::trace, args, Call::Intrinsic);
            Stmt new_body = op->body;
            new_body = Block::make(new_body, Evaluate::make(call_after));
            new_body = LetStmt::make(op->name + ".trace_id", call_before, new_body);
            stmt = Realize::make(op->name, op->types, op->bounds, op->condition, new_body);
        } else if (f.is_tracing_stores() || f.is_tracing_loads()) {
            // We need a trace id defined to pass to the loads and stores
            Stmt new_body = op->body;
            new_body = LetStmt::make(op->name + ".trace_id", 0, new_body);
            stmt = Realize::make(op->name, op->types, op->bounds, op->condition, new_body);
        }


    }

    void visit(const ProducerConsumer *op) {
        IRMutator::visit(op);
        op = stmt.as<ProducerConsumer>();
        internal_assert(op);
        map<string, Function>::const_iterator iter = env.find(op->name);
        if (iter == env.end()) return;
        Function f = iter->second;
        if (f.is_tracing_realizations() || global_level > 0) {
            // Throw a tracing call around each pipeline event
            vector<Expr> args;
            args.push_back(op->name);
            args.push_back(0);
            args.push_back(Variable::make(Int(32), op->name + ".trace_id"));
            args.push_back(0); // value index
            args.push_back(0); // value

            // Use the size of the pure step

            for (int i = 0; i < f.dimensions(); i++) {
                Expr min = Variable::make(Int(32), f.name() + ".s0." + f.args()[i] + ".min");
                Expr max = Variable::make(Int(32), f.name() + ".s0." + f.args()[i] + ".max");
                Expr extent = (max + 1) - min;
                args.push_back(min);
                args.push_back(extent);
            }

            Expr call;
            Stmt new_update;
            if (op->update.defined()) {
                args[1] = halide_trace_update;
                call = Call::make(Int(32), Call::trace, args, Call::Intrinsic);
                new_update = Block::make(Evaluate::make(call), op->update);
            }

            args[1] = halide_trace_consume;
            call = Call::make(Int(32), Call::trace, args, Call::Intrinsic);
            Stmt new_consume = Block::make(Evaluate::make(call), op->consume);

            args[1] = halide_trace_end_consume;
            call = Call::make(Int(32), Call::trace, args, Call::Intrinsic);
            new_consume = Block::make(new_consume, Evaluate::make(call));

            stmt = ProducerConsumer::make(op->name, op->produce, new_update, new_consume);

            args[1] = halide_trace_produce;
            call = Call::make(Int(32), Call::trace, args, Call::Intrinsic);
            stmt = LetStmt::make(f.name() + ".trace_id", call, stmt);
        }

    }
};

class RemoveRealizeOverOutput : public IRMutator {
    using IRMutator::visit;
    const vector<Function> &outputs;

    void visit(const Realize *op) {
        for (Function f : outputs) {
            if (op->name == f.name()) {
                stmt = mutate(op->body);
                return;
            }
        }
        IRMutator::visit(op);
    }

public:
    RemoveRealizeOverOutput(const vector<Function> &o) : outputs(o) {}
};

Stmt inject_tracing(Stmt s, const map<string, Function> &env, const vector<Function> &outputs) {
    Stmt original = s;
    InjectTracing tracing(env);

    // Add a dummy realize block for the output buffers
    for (Function output : outputs) {
        Region output_region;
        Parameter output_buf = output.output_buffers()[0];
        internal_assert(output_buf.is_buffer());
        for (int i = 0; i < output.dimensions(); i++) {
            string d = int_to_string(i);
            Expr min = Variable::make(Int(32), output_buf.name() + ".min." + d);
            Expr extent = Variable::make(Int(32), output_buf.name() + ".extent." + d);
            output_region.push_back(Range(min, extent));
        }
        s = Realize::make(output.name(), output.output_types(), output_region, const_true(), s);
    }

    // Inject tracing calls
    s = tracing.mutate(s);

    // Strip off the dummy realize blocks
    s = RemoveRealizeOverOutput(outputs).mutate(s);

    // Unless tracing was a no-op, add a call to shut down the trace
    // (which flushes the output stream)
    if (!s.same_as(original)) {
        Expr flush = Call::make(Int(32), "halide_shutdown_trace", vector<Expr>(), Call::Extern);
        s = Block::make(s, Evaluate::make(flush));
    }
    return s;
}

}
}
