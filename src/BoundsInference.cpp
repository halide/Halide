#include "BoundsInference.h"
#include "IRMutator.h"
#include "Scope.h"
#include "Bounds.h"
#include "Debug.h"
#include "IRPrinter.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Inline.h"
#include <sstream>

namespace Halide {
namespace Internal {

using std::string;
using std::ostringstream;
using std::vector;
using std::map;
using std::pair;
using std::set;

namespace {
class DependsOnBoundsInference : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *var) {
        if (ends_with(var->name, ".max") ||
            ends_with(var->name, ".min")) {
            result = true;
        }
    }

public:
    bool result;
    DependsOnBoundsInference() : result(false) {}
};

bool depends_on_bounds_inference(Expr e) {
    DependsOnBoundsInference d;
    e.accept(&d);
    return d.result;
}


struct FuncIsCalledByExpr : public IRVisitor {
    using IRVisitor::visit;

    Function f;
    bool result;
    void visit(const Call *call) {
        if (call->func.same_as(f)) {
            result = true;
        } else {
            IRVisitor::visit(call);
        }
    }
};

bool func_is_called_by_expr(Function f, Expr e) {
    FuncIsCalledByExpr c;
    c.f = f;
    c.result = false;
    e.accept(&c);
    return c.result;
}

}

class BoundsInference : public IRMutator {
public:
    const vector<Function> &funcs;
    set<string> in_pipeline, inner_productions;
    Scope<int> in_stages;

    struct Stage {
        Function func;
        int stage; // 0 is the pure definition, 1 is the first update
        string name;
        vector<int> consumers;
        map<pair<string, int>, Box> bounds;
        vector<Expr> exprs;

        // Computed expressions on the left and right-hand sides
        void compute_exprs() {
            if (stage == 0) {
                exprs = func.values();
            } else {
                const ReductionDefinition &r = func.reductions()[stage-1];
                exprs = r.values;
                exprs.insert(exprs.end(), r.args.begin(), r.args.end());
            }
        }

        // Wrap a statement in let stmts defining the box
        Stmt define_bounds(Stmt s,
                           string producing_stage,
                           const Scope<int> &in_stages,
                           const set<string> &in_pipeline,
                           const set<string> inner_productions) {
            // Merge all the relevant boxes.
            Box b;
            for (map<pair<string, int>, Box>::iterator iter = bounds.begin();
                 iter != bounds.end(); ++iter) {
                string func_name = iter->first.first;
                string stage_name = func_name + ".s" + int_to_string(iter->first.second);
                if (stage_name == producing_stage ||
                    inner_productions.count(func_name)) {
                    merge_boxes(b, iter->second);
                }
            }

            assert(b.empty() || b.size() == func.args().size());

            if (func.has_extern_definition()) {
                // After we define our bounds we need to run the
                // bounds query to define bounds for my
                // consumers.
                s = do_bounds_query(s, in_pipeline);
            }

            if (in_pipeline.count(name) == 0) {
                // Inject any explicit bounds
                string prefix = name + ".s" + int_to_string(stage) + ".";
                for (size_t i = 0; i < func.schedule().bounds.size(); i++) {
                    const Schedule::Bound &bound = func.schedule().bounds[i];
                    string min_var = prefix + bound.var + ".min";
                    string max_var = prefix + bound.var + ".max";
                    Expr min_bound = bound.min;
                    Expr max_bound = (bound.min + bound.extent) - 1;
                    s = LetStmt::make(min_var, min_bound, s);
                    s = LetStmt::make(max_var, max_bound, s);

                    // Save the unbounded values to use in bounds-checking assertions
                    Expr min_required = Variable::make(Int(32), min_var);
                    Expr max_required = Variable::make(Int(32), max_var);
                    s = LetStmt::make(min_var + "_unbounded", min_required, s);
                    s = LetStmt::make(max_var + "_unbounded", max_required, s);
                }
            }

            for (size_t d = 0; d < b.size(); d++) {
                string arg = name + ".s" + int_to_string(stage) + "." + func.args()[d];
                if (b[d].min.same_as(b[d].max)) {
                    s = LetStmt::make(arg + ".min", Variable::make(Int(32), arg + ".max"), s);
                } else {
                    s = LetStmt::make(arg + ".min", b[d].min, s);
                }
                s = LetStmt::make(arg + ".max", b[d].max, s);
            }

            if (stage > 0) {
                const ReductionDefinition &r = func.reductions()[stage-1];
                if (r.domain.defined()) {
                    const vector<ReductionVariable> &dom = r.domain.domain();
                    for (size_t i = 0; i < dom.size(); i++) {
                        string arg = name + ".s" + int_to_string(stage) + "." + dom[i].var;
                        s = LetStmt::make(arg + ".min", dom[i].min, s);
                        s = LetStmt::make(arg + ".max", dom[i].extent + dom[i].min - 1, s);
                    }
                }
            }

            return s;
        }

        Stmt do_bounds_query(Stmt s, const set<string> &in_pipeline) {
            const string &extern_name = func.extern_function_name();
            const vector<ExternFuncArgument> &args = func.extern_arguments();

            // If we're already inside a pipeline for all of the
            // inputs, this is pointless, because we know their
            // bounds.
            bool need_query = false;
            for (size_t i = 0; i < args.size(); i++) {
                if (args[i].is_func()) {
                    Function input(args[i].func);
                    if (in_pipeline.count(input.name()) == 0) {
                        need_query = true;
                    }
                }
            }
            if (!need_query) {
                return s;
            }

            vector<Expr> bounds_inference_args;

            vector<pair<string, Expr> > lets;

            // Iterate through all of the input args to the extern
            // function building a suitable argument list for the
            // extern function call.  We need a query buffer_t per
            // producer and a query buffer_t for the output

            Expr null_handle = Call::make(Handle(), Call::null_handle, vector<Expr>(), Call::Intrinsic);

            for (size_t j = 0; j < args.size(); j++) {
                if (args[j].is_expr()) {
                    bounds_inference_args.push_back(args[j].expr);
                } else if (args[j].is_func()) {
                    Function input(args[j].func);
                    for (int k = 0; k < input.outputs(); k++) {
                        string name = input.name() + ".o" + int_to_string(k) + ".bounds_query." + func.name();
                        Expr buf = Call::make(Handle(), Call::create_buffer_t,
                                              vec<Expr>(null_handle, input.output_types()[k].bytes()),
                                              Call::Intrinsic);
                        lets.push_back(make_pair(name, buf));
                        bounds_inference_args.push_back(Variable::make(Handle(), name));
                    }
                } else if (args[j].is_buffer()) {
                    Buffer b = args[j].buffer;
                    Parameter p(b.type(), true, b.name());
                    p.set_buffer(b);
                    Expr buf = Variable::make(Handle(), b.name() + ".buffer", p);
                    bounds_inference_args.push_back(buf);
                } else if (args[j].is_image_param()) {
                    Parameter p = args[j].image_param;
                    Expr buf = Variable::make(Handle(), p.name() + ".buffer", p);
                    bounds_inference_args.push_back(buf);
                } else {
                    assert(false && "Bad ExternFuncArgument type");
                }
            }

            // Make the buffer_ts representing the output. They all
            // use the same size, but have differing types.
            for (int j = 0; j < func.outputs(); j++) {
                vector<Expr> output_buffer_t_args(2);
                output_buffer_t_args[0] = null_handle;
                output_buffer_t_args[1] = func.output_types()[j].bytes();
                for (size_t k = 0; k < func.args().size(); k++) {
                    const string &arg = func.args()[k];
                    string prefix = func.name() + ".s" + int_to_string(stage) + "." + arg;
                    Expr min = Variable::make(Int(32), prefix + ".min");
                    Expr max = Variable::make(Int(32), prefix + ".max");
                    output_buffer_t_args.push_back(min);
                    output_buffer_t_args.push_back(max + 1 - min);
                    output_buffer_t_args.push_back(0); // stride
                }

                Expr output_buffer_t = Call::make(Handle(), Call::create_buffer_t,
                                                  output_buffer_t_args, Call::Intrinsic);

                string buf_name = func.name() + ".o" + int_to_string(j) + ".bounds_query";
                bounds_inference_args.push_back(Variable::make(Handle(), buf_name));

                lets.push_back(make_pair(buf_name, output_buffer_t));
            }

            // Make the extern call
            Expr e = Call::make(Int(32), extern_name,
                                bounds_inference_args, Call::Extern);
            // Check if it succeeded
            Stmt check = AssertStmt::make(EQ::make(e, 0), "Bounds inference call to external func " +
                                          extern_name + " returned non-zero value");

            // Now inner code is free to extract the fields from the buffer_t
            s = Block::make(check, s);

            // Wrap in let stmts defining the args
            for (size_t i = 0; i < lets.size(); i++) {
                s = LetStmt::make(lets[i].first, lets[i].second, s);
            }

            return s;
        }

        // A scope giving the bounds for variables used by this stage
        Scope<Interval> scope() {
            Scope<Interval> result;
            for (size_t d = 0; d < func.args().size(); d++) {
                string arg = name + ".s" + int_to_string(stage) + "." + func.args()[d];
                result.push(func.args()[d],
                            Interval(Variable::make(Int(32), arg + ".min"),
                                     Variable::make(Int(32), arg + ".max")));
            }
            if (stage > 0) {
                const ReductionDefinition &r = func.reductions()[stage-1];
                if (r.domain.defined()) {
                    const vector<ReductionVariable> &dom = r.domain.domain();
                    for (size_t i = 0; i < dom.size(); i++) {
                        const ReductionVariable &rvar = dom[i];
                        string arg = name + ".s" + int_to_string(stage) + "." + rvar.var;
                        result.push(rvar.var, Interval(Variable::make(Int(32), arg + ".min"),
                                                       Variable::make(Int(32), arg + ".max")));
                    }
                }
            }

            /*
            for (size_t i = 0; i < func.schedule().bounds.size(); i++) {
                const Schedule::Bound &b = func.schedule().bounds[i];
                result.push(b.var, Interval(b.min, (b.min + b.extent) - 1));
            }
            */

            return result;
        }

    };
    vector<Stage> stages;

    BoundsInference(const vector<Function> &f) : funcs(f) {
        assert(!f.empty());
        Function output_function = f[f.size()-1];

        // Compute the intrinsic relationships between the stages of
        // the functions.

        // Figure out which functions will be inlined away
        vector<bool> inlined(f.size());
        for (size_t i = 0; i < inlined.size(); i++) {
            if (i < f.size() - 1 &&
                f[i].schedule().compute_level.is_inline() &&
                f[i].is_pure()) {
                inlined[i] = true;
            } else {
                inlined[i] = false;
            }
        }

        // First lay out all the stages in their realization order.
        // The functions are already in topologically sorted order, so
        // this is straight-forward.
        for (size_t i = 0; i < f.size(); i++) {

            if (inlined[i]) continue;

            Stage s;
            s.func = f[i];
            s.stage = 0;
            s.name = s.func.name();
            s.compute_exprs();
            stages.push_back(s);

            for (size_t j = 0; j < f[i].reductions().size(); j++) {
                s.stage = (int)(j+1);
                s.name = s.func.name();
                s.compute_exprs();
                stages.push_back(s);
            }

        }

        // Do any pure inlining (TODO: This is currently slow)
        for (size_t i = f.size()-1; i > 0; i--) {
            Function func = f[i-1];
            if (inlined[i-1]) {
                for (size_t j = 0; j < stages.size(); j++) {
                    Stage &s = stages[j];
                    for (size_t k = 0; k < s.exprs.size(); k++) {
                        s.exprs[k] = inline_function(s.exprs[k], func);
                    }
                }
            }
        }

        // Remove the inlined stages
        vector<Stage> new_stages;
        for (size_t i = 0; i < stages.size(); i++) {
            if (stages[i].func.same_as(output_function) ||
                !stages[i].func.schedule().compute_level.is_inline() ||
                !stages[i].func.is_pure()) {
                new_stages.push_back(stages[i]);
            }
        }
        new_stages.swap(stages);

        // Dump the stages post-inlining for debugging
        /*
        debug(0) << "Bounds inference stages after inlining: \n";
        for (size_t i = 0; i < stages.size(); i++) {
            debug(0) << " " << i << ") " << stages[i].name << "\n";
        }
        */

        // Then compute relationships between them.
        for (size_t i = 0; i < stages.size(); i++) {

            Stage &consumer = stages[i];

            // Set up symbols representing the bounds over which this
            // stage will be computed.
            Scope<Interval> scope = consumer.scope();

            // Compute all the boxes of the producers this consumer
            // uses.
            map<string, Box> boxes;
            if (consumer.func.has_extern_definition()) {

                const vector<ExternFuncArgument> &args = consumer.func.extern_arguments();
                // Stage::define_bounds is going to compute a query
                // buffer_t per producer for bounds inference to
                // use. We just need to extract those values.
                for (size_t j = 0; j < args.size(); j++) {
                    if (args[j].is_func()) {
                        Function f(args[j].func);
                        string stage_name = f.name() + ".s" + int_to_string(f.reductions().size());
                        Box b(f.dimensions());
                        for (int d = 0; d < f.dimensions(); d++) {
                            string buf_name = f.name() + ".o0.bounds_query." + consumer.name;
                            Expr buf = Variable::make(Handle(), buf_name);
                            Expr min = Call::make(Int(32), Call::extract_buffer_min,
                                                  vec<Expr>(buf, d), Call::Intrinsic);
                            Expr extent = Call::make(Int(32), Call::extract_buffer_extent,
                                                     vec<Expr>(buf, d), Call::Intrinsic);
                            Expr max = min + extent - 1;
                            b[d] = Interval(min, max);
                        }
                        merge_boxes(boxes[f.name()], b);
                    }
                }

            } else {
                const vector<Expr> &exprs = consumer.exprs;
                for (size_t j = 0; j < exprs.size(); j++) {
                    map<string, Box> new_boxes = boxes_required(exprs[j], scope);
                    for (map<string, Box>::iterator iter = new_boxes.begin();
                         iter != new_boxes.end(); ++iter) {
                        merge_boxes(boxes[iter->first], iter->second);
                    }
                }
            }

            // Expand the bounds required of all the producers found.
            for (size_t j = 0; j < i; j++) {
                Stage &producer = stages[j];
                // A consumer depends on *all* stages of a producer, not just the last one.
                const Box &b = boxes[producer.func.name()];

                if (!b.empty()) {
                    // Check for unboundedness
                    for (size_t k = 0; k < b.size(); k++) {
                        if (!b[k].min.defined() || !b[k].max.defined()) {
                            if (consumer.stage == 0) {
                                std::cerr << "The pure definition ";
                            } else {
                                std::cerr << "Update definition number " << (consumer.stage-1);
                            }
                            std::cerr << " of Function " << consumer.name
                                      << " calls function " << producer.name
                                      << " in an unbounded way in dimension " << k << "\n";
                            assert(false);
                        }
                    }

                    producer.bounds[make_pair(consumer.name, consumer.stage)] = b;
                    producer.consumers.push_back((int)i);
                }
            }
        }

        // The region required of the last function is expanded to include output size
        Function output = stages[stages.size()-1].func;
        Box output_box;
        string buffer_name = output.name();
        if (output.outputs() > 1) {
            // Use the output size of the first output buffer
            buffer_name += ".0";
        }
        for (int d = 0; d < output.dimensions(); d++) {
            Expr min = Variable::make(Int(32), buffer_name + ".min." + int_to_string(d));
            Expr extent = Variable::make(Int(32), buffer_name + ".extent." + int_to_string(d));

            // Respect any output min and extent constraints
            Expr min_constraint = output.output_buffers()[0].min_constraint(d);
            Expr extent_constraint = output.output_buffers()[0].extent_constraint(d);

            if (min_constraint.defined()) {
                min = min_constraint;
            }
            if (extent_constraint.defined()) {
                extent = extent_constraint;
            }

            output_box.push_back(Interval(min, (min + extent) - 1));
        }
        for (size_t i = 0; i < stages.size(); i++) {
            Stage &s = stages[i];
            if (!s.func.same_as(output)) continue;
            s.bounds[make_pair(s.name, s.stage)] = output_box;
        }

        // Dump out the region required of each stage for debugging.
        /*
        for (size_t i = 0; i < stages.size(); i++) {
            debug(0) << "Region required of " << stages[i].name
                     << " stage " << stages[i].stage << ":\n";
            for (size_t j = 0; j < stages[i].bounds.size(); j++) {
                debug(0) << "  [" << simplify(stages[i].bounds[j].min) << ", " << simplify(stages[i].bounds[j].max) << "]\n";
            }
            debug(0) << " consumed by: ";
            for (size_t j = 0; j < stages[i].consumers.size(); j++) {
                debug(0) << stages[stages[i].consumers[j]].name << " ";
            }
            debug(0) << "\n";
        }
        */
    }

    using IRMutator::visit;

    void visit(const For *op) {
        set<string> old_inner_productions;
        inner_productions.swap(old_inner_productions);

        Stmt body = op->body;

        // Walk inside of any let statements that don't depend on
        // bounds inference results so that we don't needlessly
        // complicate our bounds expressions.
        vector<pair<string, Expr> > lets;
        while (const LetStmt *let = body.as<LetStmt>()) {
            if (depends_on_bounds_inference(let->value)) {
                break;
            }

            body = let->body;
            lets.push_back(make_pair(let->name, let->value));
        }

        // Figure out which stage of which function we're producing
        int producing = -1;
        Function f;
        string stage_name;
        for (size_t i = 0; i < stages.size(); i++) {
            string next_stage_name = stages[i].name + ".s" + int_to_string(stages[i].stage);
            if (starts_with(op->name, next_stage_name + ".")) {
                producing = i;
                f = stages[i].func;
                stage_name = next_stage_name;
            }
        }

        in_stages.push(stage_name, 0);

        // Figure out how much of it we're producing
        Box box;
        if (producing >= 0) {
            box = box_provided(body, stages[producing].name);
            assert((int)box.size() == f.dimensions());
        }

        // Recurse.
        body = mutate(body);

        // We only care about the bounds of things that have a
        // production inside this loop somewhere, and their
        // consumers that we're not already in a pipeline of.
        vector<bool> bounds_needed(stages.size(), false);

        for (size_t i = 0; i < stages.size(); i++) {
            if (inner_productions.count(stages[i].name)) {
                bounds_needed[i] = true;
            }

            if (in_pipeline.count(stages[i].name)) {
                bounds_needed[i] = false;
            }

            if (bounds_needed[i]) {
                for (size_t j = 0; j < stages[i].consumers.size(); j++) {
                    bounds_needed[stages[i].consumers[j]] = true;
                }
                body = stages[i].define_bounds(body, stage_name, in_stages, in_pipeline, inner_productions);
            }
        }

        // Finally, define the production bounds for the thing
        // we're producing.
        if (producing >= 0 && !inner_productions.empty()) {
            for (size_t i = 0; i < box.size(); i++) {
                assert(box[i].min.defined() && box[i].max.defined());
                string var = stage_name + "." + f.args()[i];

                if (box[i].max.same_as(box[i].min)) {
                    body = LetStmt::make(var + ".max", Variable::make(Int(32), var + ".min"), body);
                } else {
                    body = LetStmt::make(var + ".max", box[i].max, body);
                }

                body = LetStmt::make(var + ".min", box[i].min, body);
            }
        }

        // And the current bounds on its reduction variables.
        if (producing >= 0 && stages[producing].stage > 0) {
            const Stage &s = stages[producing];
            const ReductionDefinition &r = s.func.reductions()[s.stage-1];
            if (r.domain.defined()) {
                const vector<ReductionVariable> &d = r.domain.domain();
                for (size_t i = 0; i < d.size(); i++) {
                    if (op->name == s.name + ".s" + int_to_string(s.stage) + "." + d[i].var) {
                        // We just entered the loop over this var
                        Expr loop_var = Variable::make(Int(32), op->name);
                        body = LetStmt::make(op->name + ".min", loop_var, body);
                        body = LetStmt::make(op->name + ".max", loop_var, body);
                    }
                }
            }
        }

        inner_productions.insert(old_inner_productions.begin(),
                                 old_inner_productions.end());

        // Rewrap the let statements
        for (size_t i = lets.size(); i > 0; i--) {
            body = LetStmt::make(lets[i-1].first, lets[i-1].second, body);
        }

        in_stages.pop(stage_name);

        stmt = For::make(op->name, op->min, op->extent, op->for_type, body);
    }

    void visit(const Pipeline *p) {
        in_pipeline.insert(p->name);
        IRMutator::visit(p);
        in_pipeline.erase(p->name);
        inner_productions.insert(p->name);
    }

};



Stmt bounds_inference(Stmt s, const vector<string> &order,
                      const map<string, Function> &env) {

    vector<Function> funcs(order.size());
    for (size_t i = 0; i < order.size(); i++) {
        funcs[i] = env.find(order[i])->second;
    }

    // Add an outermost bounds inference marker
    s = For::make("<outermost>", 0, 1, For::Serial, s);
    s = BoundsInference(funcs).mutate(s);
    return s.as<For>()->body;
}



}
}
