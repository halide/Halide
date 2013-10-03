#include "BoundsInference.h"
#include "IRMutator.h"
#include "Scope.h"
#include "Bounds.h"
#include "Debug.h"
#include "IRPrinter.h"
#include "IROperator.h"
#include "Simplify.h"
#include <sstream>

namespace Halide {
namespace Internal {

using std::string;
using std::ostringstream;
using std::vector;
using std::map;
using std::pair;

// Inject let stmts defining the bounds of a function required at each loop level
class BoundsInference : public IRMutator {
public:
    const vector<Function> &funcs;
    Scope<int> in_consume, in_realize;

    BoundsInference(const vector<Function> &f) : funcs(f) {
    }

    using IRMutator::visit;

    virtual void visit(const For *for_loop) {

        debug(3) << "\nIn loop over " << for_loop->name << " computing regions called...\n\n";

        // Compute the region required of each function within this loop body

        map<string, Region> regions = regions_called(for_loop->body);

        Stmt body = mutate(for_loop->body);

        debug(2) << "Bound inference considering for loop over " << for_loop->name << "\n";

        debug(3) << "\nIn loop over " << for_loop->name << " regions called are:\n\n";
        for (size_t i = 0; i < funcs.size(); i++) {
            map<string, Region>::const_iterator iter = regions.find(funcs[i].name());
            if (iter == regions.end()) continue;
            const Region &r = iter->second;

            debug(3) << funcs[i].name() << ":\n";
            for (size_t j = 0; j < r.size(); j++) {
                debug(3) << "  min " << j << ": " << r[j].min << "\n"
                         << "  extent " << j << ": " << r[j].extent << "\n";
            }
            debug(3) << "\n";
        }

        // In the outermost loop, the output func counts as used
        if (for_loop->name == "<outermost>") {
            // For the output function, the bounds required is the (possibly
            // constrained) size of the first output buffer. (TODO: check the
            // other output buffers match this size).
            const Function &f = funcs[funcs.size()-1];
            Region region;

            Parameter b = f.output_buffers()[0];
            for (size_t i = 0; i < f.args().size(); i++) {

                string prefix = f.name() + "." + f.args()[i];
                string dim = int_to_string(i);

                Expr min, extent;
                if (b.min_constraint(i).defined()) {
                    min = b.min_constraint(i);
                } else {
                    string buf_min_name = b.name() + ".min." + dim;
                    min = Variable::make(Int(32), buf_min_name);
                }

                if (b.extent_constraint(i).defined()) {
                    extent = b.extent_constraint(i);
                } else {
                    string buf_extent_name = b.name() + ".extent." + dim;
                    extent = Variable::make(Int(32), buf_extent_name);
                }

                region.push_back(Range(min, extent));
            }

            map<string, Region>::iterator iter = regions.find(f.name());
            if (iter != regions.end()) {
                iter->second = region_union(iter->second, region);
            } else {
                regions[f.name()] = region;
            }

        }

        debug(3) << "Bounds inference considering loop over " << for_loop->name << '\n';

        // Inject let statements defining those bounds
        for (size_t i = 0; i < funcs.size(); i++) {
            if (in_consume.contains(funcs[i].name())) continue;

            map<string, Region>::iterator iter = regions.find(funcs[i].name());
            bool func_used = iter != regions.end();
            Region region;
            if (func_used) region = iter->second;
            const Function &f = funcs[i];

            debug(3) << "Injecting bounds for " << funcs[i].name() << '\n';

            // If this func f is an input to an extern stage called g,
            // take a region union with the result of bounds inference
            // on g.
            for (size_t j = i+1; j < funcs.size(); j++) {
                debug(4) << "Checking to see if " << f.name() << " is used by an extern stage\n";
                bool used_by_extern = false;
                const Function &g = funcs[j];

                // g isn't used in this loop, so don't bother.
                if (regions.find(funcs[j].name()) == regions.end()) continue;

                if (g.has_extern_definition()) {
                    for (size_t k = 0; k < g.extern_arguments().size(); k++) {
                        ExternFuncArgument arg = g.extern_arguments()[k];
                        if (!arg.is_func()) continue;
                        Function input(arg.func);
                        if (input.same_as(f)) {
                            used_by_extern = true;
                        }
                    }
                }
                if (used_by_extern) {
                    func_used = true;
                    debug(4) << f.name() << " is used by extern stage " << g.name() << "\n";
                    // g is an extern func that takes f as an input,
                    // so we need to expand the bounds f using the
                    // result of the bounds inference query. If f has
                    // multiple outputs, we should union the bounds
                    // query results from each output.
                    for (int output = 0; output < f.outputs(); output++) {
                        Region extern_region;
                        for (int k = 0; k < f.dimensions(); k++) {
                            string buf_name = f.name();
                            if (f.outputs() > 1) {
                                buf_name += "." + int_to_string(output);
                            }
                            buf_name += "." + g.name() + ".bounds";
                            Expr buf = Variable::make(Handle(), buf_name);
                            Expr min = Call::make(Int(32), Call::extract_buffer_min,
                                                  vec<Expr>(buf, k), Call::Intrinsic);
                            Expr extent = Call::make(Int(32), Call::extract_buffer_extent,
                                                     vec<Expr>(buf, k), Call::Intrinsic);
                            extern_region.push_back(Range(min, extent));
                        }
                        if (region.empty()) {
                            region = extern_region;
                        } else {
                            region = region_union(region, extern_region);
                        }
                    }
                } else {
                    debug(4) << f.name() << " is not used by any extern stage\n";
                }

            }

            if (!func_used) continue;
            assert(region.size() == (size_t)f.dimensions() &&
                   "Dimensionality mismatch between function and region required");

            // If this function g is defined using an extern stage,
            // construct query buffer_ts for the inputs (all zeroes),
            // and a query buffer_t for the output (using g.x.min,
            // etc), and define symbols of the form f.x.extern_min.g
            // (assuming the input function is called f). These
            // symbols will be referenced by the bounds computations
            // of the input stages (because region_called will use
            // those symbols when it encounters an extern stage).
            if (f.has_extern_definition()) {

                const vector<ExternFuncArgument> &args = f.extern_arguments();

                // If we're already inside the consume stages of all
                // of the Func inputs, this is pointless, because
                // nobody is going to use the results of this query.
                bool should_proceed = false;
                for (size_t j = 0; j < args.size(); j++) {
                    if (args[j].is_func()) {
                        Function input(args[j].func);
                        if (!in_consume.contains(input.name())) {
                            should_proceed = true;
                        }
                    }
                }

                if (should_proceed) {
                    vector<Expr> bounds_inference_args;
                    const string &extern_name = f.extern_function_name();

                    vector<pair<string, Expr> > lets;

                    // Iterate through all of the input args to the extern
                    // function building a suitable argument list for the
                    // extern function call.
                    for (size_t j = 0; j < args.size(); j++) {
                        if (args[j].is_expr()) {
                            bounds_inference_args.push_back(args[j].expr);
                        } else if (args[j].is_func()) {
                            Function input(args[j].func);
                            for (int k = 0; k < input.outputs(); k++) {
                                string name = input.name();
                                if (input.outputs() > 1) {
                                    name += "." + int_to_string(k);
                                }
                                name += "." + f.name() + ".bounds";

                                Expr buf = Call::make(Handle(), Call::create_buffer_t,
                                                      vec<Expr>(0, input.output_types()[k].bytes()),
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

                    // Make the buffer_ts representing the output. They
                    // all use the same size, but have differing types.
                    for (int j = 0; j < f.outputs(); j++) {
                        vector<Expr> output_buffer_t_args(2);
                        output_buffer_t_args[0] = 0;
                        output_buffer_t_args[1] = f.output_types()[j].bytes();
                        for (size_t k = 0; k < region.size(); k++) {
                            const string &arg = f.args()[k];
                            Expr min = Variable::make(Int(32), f.name() + "." + arg + ".min_required");
                            Expr extent = Variable::make(Int(32), f.name() + "." + arg + ".extent_required");
                            Expr stride = 0;
                            output_buffer_t_args.push_back(min);
                            output_buffer_t_args.push_back(extent);
                            output_buffer_t_args.push_back(stride);
                        }

                        Expr output_buffer_t = Call::make(Handle(), Call::create_buffer_t,
                                                          output_buffer_t_args, Call::Intrinsic);

                        string buf_name = f.name();
                        if (f.outputs() > 1) {
                            buf_name += "." + int_to_string(j);
                        }
                        buf_name += ".bounds";
                        bounds_inference_args.push_back(Variable::make(Handle(), buf_name));

                        lets.push_back(make_pair(buf_name, output_buffer_t));
                    }

                    debug(4) << "Building call to extern func " << f.name()
                             << " for the purposes of bounds inference\n";

                    // Make the extern call
                    Expr e = Call::make(Int(32), extern_name,
                                        bounds_inference_args, Call::Extern);
                    // Check if it succeeded
                    Stmt check = AssertStmt::make(EQ::make(e, 0), "Bounds inference call to external func " +
                                                  extern_name + " returned non-zero value");

                    // Now inner code is free to extract the fields from the buffer_t
                    body = Block::make(check, body);

                    // Wrap in let stmts defining the args
                    for (size_t i = 0; i < lets.size(); i++) {
                        body = LetStmt::make(lets[i].first, lets[i].second, body);
                    }
                }
            }

            for (size_t j = 0; j < region.size(); j++) {
                if (!region[j].min.defined() || !region[j].extent.defined()) {
                    std::cerr << "Use of " << f.name()
                              << " is unbounded in dimension " << j
                              << " in the following fragment of generated code: \n"
                              << for_loop->body;
                    assert(false);
                }

                const string &arg_name = f.args()[j];
                string prefix = f.name() + "." + arg_name;
                string min_required_name = prefix + ".min_required";
                string extent_required_name = prefix + ".extent_required";

                Expr min_required = region[j].min;
                Expr extent_required = region[j].extent;
                Expr min_required_var = Variable::make(Int(32), min_required_name);
                Expr extent_required_var = Variable::make(Int(32), extent_required_name);
                Expr max_min_var = Variable::make(Int(32), prefix + ".max_min");

                // Compute the range produced as a function of the range required.
                // TODO: Move the code that applies an explicit bound here
                string extent_produced_name = prefix + ".extent_produced";
                string min_produced_name = prefix + ".min_produced";
                Expr extent_produced_var = Variable::make(Int(32), extent_produced_name);
                Expr min_produced_var = Variable::make(Int(32), min_produced_name);

                string extent_realized_name = prefix + ".extent_realized";
                string min_realized_name = prefix + ".min_realized";
                Expr extent_realized_var = Variable::make(Int(32), extent_realized_name);
                Expr min_realized_var = Variable::make(Int(32), min_realized_name);

                Expr min_extent = f.min_extent_produced(arg_name);

                // Round up the extent to cover the min production size and factor
                Expr extent_produced = extent_required_var;
                if (!is_one(min_extent)) {
                    extent_produced = Max::make(extent_required_var, min_extent);
                }

                Expr min_extent_factor = f.min_extent_updated(arg_name);
                if (!is_one(min_extent_factor)) {
                    extent_produced += min_extent_factor - 1;
                    extent_produced /= min_extent_factor;
                    extent_produced *= min_extent_factor;
                }

                // Push the min down to be less than the max min.
                Expr min_produced = min_required_var;
                if (for_loop->name != "<outermost>") {
                    if (!extent_produced.same_as(extent_required_var)) {
                        min_produced = Min::make(min_produced, max_min_var);
                    }
                }

                // The maximum min for inner realizations is min_extent less than the extent produced at this level.
                if (!in_realize.contains(f.name())) {
                    body = LetStmt::make(prefix + ".max_min", min_produced_var + extent_produced_var - min_extent, body);
                }
                // Assign the range produced as a function of the range required
                body = LetStmt::make(extent_produced_name, extent_produced, body);
                body = LetStmt::make(min_produced_name, min_produced, body);

                // For the output function, the bounds realized need
                // to be defined somewhere. They're the same as the
                // region required. I.e. if you pass in a buffer of a
                // certain size, you are requesting exactly that much
                // data. In this future we may wish to distinguish
                // between the allocated size and the region
                // requested.
                if (i == funcs.size()-1 && for_loop->name == "<outermost>") {
                    Expr max_min = min_required_var + extent_required_var - min_extent;
                    // TODO: Shouldn't this be related to the range produced, not required?
                    body = LetStmt::make(prefix + ".max_min", max_min, body);
                    body = LetStmt::make(prefix + ".min_realized", min_required_var, body);
                    body = LetStmt::make(prefix + ".extent_realized", extent_required_var, body);
                }

                // Assign the range required as a function of the region called by the body.
                body = LetStmt::make(min_required_name, min_required, body);
                body = LetStmt::make(extent_required_name, extent_required, body);

                debug(3) << "Assigning " << min_required_name << " and " << extent_required_name << "\n";

            }

        }

        if (body.same_as(for_loop->body)) {
            stmt = for_loop;
        } else {
            stmt = For::make(for_loop->name, for_loop->min, for_loop->extent, for_loop->for_type, body);
        }

        debug(3) << "New version of loop: \n" << stmt << "\n\n";
    }

    virtual void visit(const Pipeline *pipeline) {
        Stmt produce = mutate(pipeline->produce);

        // If we're in the consumption of a func, we no longer care about its bounds.
        in_consume.push(pipeline->name, 0);
        Stmt update;
        if (pipeline->update.defined()) {
            update = mutate(pipeline->update);
        }
        Stmt consume = mutate(pipeline->consume);
        in_consume.pop(pipeline->name);

        stmt = Pipeline::make(pipeline->name, produce, update, consume);
    }

    virtual void visit(const Realize *realize) {
        in_realize.push(realize->name, 0);
        IRMutator::visit(realize);
        in_realize.pop(realize->name);
    }

};

Stmt bounds_inference(Stmt s, const vector<string> &order, const map<string, Function> &env) {
    // Add a outermost::make loop to make sure we get outermost bounds definitions too
    s = For::make("<outermost>", 0, 1, For::Serial, s);

    vector<Function> funcs(order.size());
    for (size_t i = 0; i < order.size(); i++) {
        funcs[i] = env.find(order[i])->second;
    }

    s = BoundsInference(funcs).mutate(s);

    // We can remove the loop over root now
    const For *root_loop = s.as<For>();
    assert(root_loop);
    s = root_loop->body;

    return s;
}



}
}
