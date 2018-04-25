#include "Tracing.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "runtime/HalideRuntime.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::map;
using std::string;
using std::pair;
using std::set;

struct TraceEventBuilder {
    string func, trace_tag;
    vector<Expr> value;
    vector<Expr> coordinates;
    Type type;
    enum halide_trace_event_code_t event;
    Expr parent_id, value_index;

    Expr build() {
        Expr values = Call::make(type_of<void *>(), Call::make_struct,
                                 value, Call::Intrinsic);
        Expr coords = Call::make(type_of<int32_t *>(), Call::make_struct,
                                 coordinates, Call::Intrinsic);
        Expr idx = value_index;
        if (!idx.defined()) {
            idx = 0;
        }

        vector<Expr> args = {Expr(func),
                             values, coords,
                             (int)type.code(), (int)type.bits(), (int)type.lanes(),
                             (int)event,
                             parent_id, idx, (int)coordinates.size(),
                             Expr(trace_tag)};
        return Call::make(Int(32), Call::trace, args, Call::Extern);
    }
};

class InjectTracing : public IRMutator2 {
public:
    const map<string, Function> &env;
    const bool trace_all_loads, trace_all_stores, trace_all_realizations;
    // We want to preserve the order, so use a vector<pair> rather than a map
    vector<pair<string, vector<string>>> trace_tags;
    set<string> trace_tags_added;

    InjectTracing(const map<string, Function> &e, const Target &t)
        : env(e),
          trace_all_loads(t.has_feature(Target::TraceLoads)),
          trace_all_stores(t.has_feature(Target::TraceStores)),
          // Set trace_all_realizations to true if either trace_loads or trace_stores is on too:
          // They don't work without trace_all_realizations being on (and the errors are missing symbol mysterious nonsense).
          trace_all_realizations(t.features_any_of({Target::TraceLoads, Target::TraceStores, Target::TraceRealizations})) {
    }

private:
    void add_trace_tags(const string &name, const vector<string> &t) {
        if (!t.empty() && !trace_tags_added.count(name)) {
            trace_tags.push_back({name, t});
            trace_tags_added.insert(name);
        }
    }

    using IRMutator2::visit;

    Expr visit(const Call *op) override {
        Expr expr = IRMutator2::visit(op);
        op = expr.as<Call>();
        internal_assert(op);
        bool trace_it = false;
        Expr trace_parent;
        if (op->call_type == Call::Halide) {
            auto it = env.find(op->name);
            internal_assert(it != env.end()) << op->name << " not in environment\n";
            Function f = it->second;
            internal_assert(!f.can_be_inlined() || !f.schedule().compute_level().is_inlined());

            trace_it = f.is_tracing_loads() || trace_all_loads;
            trace_parent = Variable::make(Int(32), op->name + ".trace_id");
            if (trace_it) {
                add_trace_tags(op->name, f.get_trace_tags());
            }
        } else if (op->call_type == Call::Image) {
            trace_it = trace_all_loads;
            // If there is a Function in the env named "name_im", assume that
            // this image is an ImageParam, so sniff that Function to see
            // if we want to trace loads on it. (This allows us to trace
            // loads on inputs without having to enable them globally.)
            auto it = env.find(op->name + "_im");
            if (it != env.end()) {
                Function f = it->second;
                // f could be scheduled and have actual loads from it (via ImageParam::in),
                // so only honor trace the loads if it is inlined.
                if ((f.is_tracing_loads() || trace_all_loads) &&
                    f.can_be_inlined() &&
                    f.schedule().compute_level().is_inlined()) {
                    trace_it = true;
                    add_trace_tags(op->name, f.get_trace_tags());
                }
            }

            trace_parent = Variable::make(Int(32), "pipeline.trace_id");
        }

        if (trace_it) {
            string value_var_name = unique_name('t');
            Expr value_var = Variable::make(op->type, value_var_name);

            TraceEventBuilder builder;
            builder.func = op->name;
            builder.value = {value_var};
            builder.coordinates = op->args;
            builder.type = op->type;
            builder.event = halide_trace_load;
            builder.parent_id = trace_parent;
            builder.value_index = op->value_index;
            Expr trace = builder.build();

            expr = Let::make(value_var_name, op,
                             Call::make(op->type, Call::return_second,
                                        {trace, value_var}, Call::PureIntrinsic));
        }
        return expr;
    }

    Stmt visit(const Provide *op) override {
        Stmt stmt = IRMutator2::visit(op);
        op = stmt.as<Provide>();
        internal_assert(op);

        map<string, Function>::const_iterator iter = env.find(op->name);
        if (iter == env.end()) return stmt;
        Function f = iter->second;
        internal_assert(!f.can_be_inlined() || !f.schedule().compute_level().is_inlined());

        if (f.is_tracing_stores() || trace_all_stores) {
            // Wrap each expr in a tracing call

            const vector<Expr> &values = op->values;
            vector<Expr> traces(op->values.size());

            TraceEventBuilder builder;
            builder.func = f.name();
            builder.coordinates = op->args;
            builder.event = halide_trace_store;
            builder.parent_id = Variable::make(Int(32), op->name + ".trace_id");
            for (size_t i = 0; i < values.size(); i++) {
                Type t = values[i].type();
                string value_var_name = unique_name('t');
                Expr value_var = Variable::make(t, value_var_name);

                builder.type = t;
                builder.value_index = (int)i;
                builder.value = {value_var};
                Expr trace = builder.build();

                traces[i] = Let::make(value_var_name, values[i],
                                      Call::make(t, Call::return_second,
                                                 {trace, value_var}, Call::PureIntrinsic));
            }

            // Lift the args out into lets so that the order of
            // evaluation is right for scatters. Otherwise the store
            // is traced before any loads in the index.
            vector<Expr> args = op->args;
            vector<pair<string, Expr>> lets;
            for (size_t i = 0; i < args.size(); i++) {
                if (!args[i].as<Variable>() && !is_const(args[i])) {
                    string name = unique_name('t');
                    lets.push_back({name, args[i]});
                    args[i] = Variable::make(args[i].type(), name);
                }
            }

            stmt = Provide::make(op->name, traces, args);
            for (const auto &p : lets) {
                stmt = LetStmt::make(p.first, p.second, stmt);
            }
        }
        return stmt;
    }

    Stmt visit(const Realize *op) override {
        Stmt stmt = IRMutator2::visit(op);
        op = stmt.as<Realize>();
        internal_assert(op);

        map<string, Function>::const_iterator iter = env.find(op->name);
        if (iter == env.end()) return stmt;
        Function f = iter->second;
        if (f.is_tracing_realizations() || trace_all_realizations) {
            add_trace_tags(op->name, f.get_trace_tags());

            // Throw a tracing call before and after the realize body
            TraceEventBuilder builder;
            builder.func = op->name;
            builder.parent_id = Variable::make(Int(32), "pipeline.trace_id");
            builder.event = halide_trace_begin_realization;

            for (size_t i = 0; i < op->bounds.size(); i++) {
                builder.coordinates.push_back(op->bounds[i].min);
                builder.coordinates.push_back(op->bounds[i].extent);
            }

            // Begin realization returns a unique token to pass to further trace calls affecting this buffer.
            Expr call_before = builder.build();
            builder.event = halide_trace_end_realization;
            builder.parent_id = Variable::make(Int(32), op->name + ".trace_id");
            Expr call_after = builder.build();
            Stmt new_body = op->body;
            new_body = Block::make(new_body, Evaluate::make(call_after));
            new_body = LetStmt::make(op->name + ".trace_id", call_before, new_body);
            stmt = Realize::make(op->name, op->types, op->memory_type, op->bounds, op->condition, new_body);
            // Warning: 'op' may be invalid at this point
        } else if (f.is_tracing_stores() || f.is_tracing_loads()) {
            // We need a trace id defined to pass to the loads and stores
            Stmt new_body = op->body;
            new_body = LetStmt::make(op->name + ".trace_id", 0, new_body);
            stmt = Realize::make(op->name, op->types, op->memory_type, op->bounds, op->condition, new_body);
        }
        return stmt;
    }

    Stmt visit(const ProducerConsumer *op) override {
        Stmt stmt = IRMutator2::visit(op);
        op = stmt.as<ProducerConsumer>();
        internal_assert(op);
        map<string, Function>::const_iterator iter = env.find(op->name);
        if (iter == env.end()) return stmt;
        Function f = iter->second;
        if (f.is_tracing_realizations() || trace_all_realizations) {
            // Throw a tracing call around each pipeline event
            TraceEventBuilder builder;
            builder.func = op->name;
            builder.parent_id = Variable::make(Int(32), op->name + ".trace_id");

            // Use the size of the pure step
            const vector<string> f_args = f.args();
            for (int i = 0; i < f.dimensions(); i++) {
                Expr min = Variable::make(Int(32), f.name() + ".s0." + f_args[i] + ".min");
                Expr max = Variable::make(Int(32), f.name() + ".s0." + f_args[i] + ".max");
                Expr extent = (max + 1) - min;
                builder.coordinates.push_back(min);
                builder.coordinates.push_back(extent);
            }

            builder.event = (op->is_producer ?
                             halide_trace_produce :
                             halide_trace_consume);
            Expr begin_op_call = builder.build();

            builder.event = (op->is_producer ?
                             halide_trace_end_produce :
                             halide_trace_end_consume);
            Expr end_op_call = builder.build();


            Stmt new_body = Block::make(op->body, Evaluate::make(end_op_call));

            stmt = LetStmt::make(f.name() + ".trace_id", begin_op_call,
                                 ProducerConsumer::make(op->name, op->is_producer, new_body));
        }
        return stmt;
    }
};

class RemoveRealizeOverOutput : public IRMutator2 {
    using IRMutator2::visit;
    const vector<Function> &outputs;

    Stmt visit(const Realize *op) override {
        for (Function f : outputs) {
            if (op->name == f.name()) {
                return mutate(op->body);
            }
        }
        return IRMutator2::visit(op);
    }

public:
    RemoveRealizeOverOutput(const vector<Function> &o) : outputs(o) {}
};

Stmt inject_tracing(Stmt s, const string &pipeline_name,
                    const map<string, Function> &env, const vector<Function> &outputs,
                    const Target &t) {
    Stmt original = s;
    InjectTracing tracing(env, t);

    // Add a dummy realize block for the output buffers
    for (Function output : outputs) {
        Region output_region;
        Parameter output_buf = output.output_buffers()[0];
        internal_assert(output_buf.is_buffer());
        for (int i = 0; i < output.dimensions(); i++) {
            string d = std::to_string(i);
            Expr min = Variable::make(Int(32), output_buf.name() + ".min." + d);
            Expr extent = Variable::make(Int(32), output_buf.name() + ".extent." + d);
            output_region.push_back(Range(min, extent));
        }
        s = Realize::make(output.name(), output.output_types(), MemoryType::Auto, output_region, const_true(), s);
    }

    // Inject tracing calls
    s = tracing.mutate(s);

    // Strip off the dummy realize blocks
    s = RemoveRealizeOverOutput(outputs).mutate(s);

    if (!s.same_as(original)) {
        // Add pipeline start and end events
        TraceEventBuilder builder;
        builder.func = pipeline_name;
        builder.event = halide_trace_begin_pipeline;
        builder.parent_id = 0;

        Expr pipeline_start = builder.build();

        builder.event = halide_trace_end_pipeline;
        builder.parent_id = Variable::make(Int(32), "pipeline.trace_id");
        Expr pipeline_end = builder.build();

        s = Block::make(s, Evaluate::make(pipeline_end));

        // All trace_tag events go at the start, immediately after begin_pipeline.
        // For a given realization/input/output, we output them in the order
        // we encounter them (which is to say, the order they were added); however,
        // we don't attempt to preserve a particular order between functions.
        for (const auto &trace_tags : tracing.trace_tags) {
            // builder.parent_id is already set correctly
            builder.func = trace_tags.first;  // func name
            builder.event = halide_trace_tag;
            // We must reverse-iterate to preserve order
            for (auto it = trace_tags.second.rbegin(); it != trace_tags.second.rend(); ++it) {
                user_assert(it->find('\0') == string::npos)
                    << "add_trace_tag() may not contain the null character.";
                builder.trace_tag = *it;
                s = Block::make(Evaluate::make(builder.build()), s);
            }
        }

        s = LetStmt::make("pipeline.trace_id", pipeline_start, s);
    }

    return s;
}

}
}
