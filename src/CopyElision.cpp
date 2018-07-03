#include "CopyElision.h"
#include "IREquality.h"
#include "Func.h"
#include "FindCalls.h"
#include "Schedule.h"
#include "AutoScheduleUtils.h"

#include <set>

namespace Halide {
namespace Internal {

using std::map;
using std::set;
using std::string;
using std::vector;

namespace {

/** If function 'f' operation only involves pointwise copy from another
  * function, return the name of the function from which it copies from.
  * If the function being copied from is a tuple, we have to ensure that 'f'
  * copies the whole tuple and not only some of the tuple values; otherwise,
  * treat it as non pointwise copies. For non pointwise copy or if 'f' has
  * update definitions or is an extern function, return an empty string.
  */
string get_pointwise_copy_producer(const Function &f,
                                   const map<string, Function> &env,
                                   const map<string, int> &num_callers,
                                   const set<string> &inlined) {

    if (f.has_update_definition()) {
        return "";
    }

    if (f.has_extern_definition()) {
        return "";
    }

    /*if (f.has_extern_definition() &&
        (f.extern_function_name() == "halide_buffer_copy")) {
        // TODO(psuriana): Check if this extern function is actually a
        // a buffer copy
        // TODO(psuriana): How do you handle Tuple for buffer copy?
        string prod;
        for (const ExternFuncArgument &arg : f.extern_arguments()) {
            if (arg.is_func()) {
                Function g(arg.func);
                if (!prod.empty() && (prod != g.name())) {
                    debug(4) << "...Extern function \"" << f.name() << "\" copies multiple "
                             << "functions: \"" << prod << "\" and \""
                             << g.name() << "\"\n";
                    return "";
                }
                prod = g.name();
            }
        }
        if (!prod.empty()) {
            debug(4) << "...Found halide_buffer_copy -> " << print_function(f) << "\n";
        }
        return prod;
    }*/

    debug(0) << "\n\nGET POINTWISE COPIES FOR: " << f.name() << "\n";
    string prod;
    for (int i = 0; i < (int)f.values().size(); ++i) {
        Expr val = perform_inline(f.values()[i], env, inlined);
        if (const Call *call = val.as<Call>()) {
            if (call->call_type == Call::Halide) {
                debug(0) << "\t...Checking call: " << Expr(call) << "\n";
                // Check if it is a pointwise copy. For tuple, check if 'f'
                // copies the whole tuple values.
                if (!prod.empty() && (prod != call->name)) {
                    debug(4) << "...Function \"" << f.name() << "\" calls multiple "
                             << "functions: \"" << prod << "\" and \""
                             << call->name << "\"\n";
                    return "";
                }
                prod = call->name;

                // Check if only 'f' calls 'prod'
                const auto &iter = num_callers.find(prod);
                if ((iter != num_callers.end()) && (iter->second > 1)) {
                    debug(4) << "...Function \"" << f.name() << "\" (" << print_function(f)
                             << ") is pointwise copies but \""
                             << prod << "\" has multiple callers\n";
                    return "";
                }

                Function prod_f = Function(call->func);
                if (f.dimensions() != prod_f.dimensions()) {
                    debug(4) << "...Function \"" << f.name() << "\" and \""
                             << prod_f.name() << "\" have different dimensions ("
                             << f.dimensions() << " vs " << prod_f.dimensions() << ")\n";
                    return "";
                }
                internal_assert(f.args().size() == call->args.size());

                if (f.values().size() != prod_f.values().size()) {
                    debug(4) << "...Function \"" << f.name() << "\" does not call "
                             << "the whole tuple values of function \""
                             << prod_f.name() << "\"(" << f.values().size()
                             << " vs " << prod_f.values().size() << ")\n";
                    return "";
                }

                if (i != call->value_index) {
                    debug(4) << "...Function \"" << f.name() << "\" calls "
                             << prod_f.name() << "[" << call->value_index
                             << "] at value index " << i << "\n";
                    return "";
                }

                for (int j = 0; j < f.dimensions(); ++j) {
                    // Check if the call args are equivalent for both the
                    // RHS ('f') and LHS ('prod_f').
                    // TODO(psuriana): Handle case for copy with some index shifting
                    debug(0) << "\tcons: " << f.name() << ", cons arg: " << f.args()[j] << ", prod: " << prod_f.name() << ", prod arg: " << call->args[j] << "\n";
                    if (!equal(f.args()[j], call->args[j])) {
                        debug(0) << "At arg " << j << ", " << f.name() << "("
                                 << f.args()[i] << ") != " << prod_f.name()
                                 << "[" << call->value_index << "]("
                                 << call->args[j] << ")\n";
                        return "";
                    }
                }

                /*for (int j = 0; j < f.dimensions(); ++j) {
                    // Check if the call args are equivalent for both the
                    // RHS ('f') and LHS ('prod_f').
                    // TODO(psuriana): Handle case for copy with some index shifting
                    debug(0) << "\tcons: " << f.name() << ", cons arg: " << f.args()[j] << ", prod: " << prod_f.name() << ", prod arg: " << prod_f.args()[j] << "\n";
                    if (!equal(f.args()[j], prod_f.args()[j])) {
                        debug(0) << "At arg " << j << ", " << f.name() << "("
                                 << f.args()[i] << ") != " << prod_f.name()
                                 << "[" << call->value_index << "]("
                                 << prod_f.args()[j] << ")\n";
                        return "";
                    }
                }*/
            }
        } else if (!prod.empty()) {
            debug(4) << "...Function \"" << f.name() << "\" does not call "
                     << "the whole tuple values of function \""
                     << prod << "\" or is not a simple copy\n";
            return "";
        }
    }

    if (!prod.empty()) {
        debug(4) << "...Found pointwise copy -> " << print_function(f) << "\n";
    }
    return prod;
}

} // anonymous namespace

string print_function(const Function &f) {
    std::ostringstream stream;
    stream << f.name() << "(";
    for (int i = 0; i < f.dimensions(); ++i) {
        stream << f.args()[i];
        if (i != f.dimensions()-1) {
            stream << ", ";
        }
    }
    stream << ") = ";

    if (f.has_extern_definition()) {
        vector<Expr> extern_call_args;
        const vector<ExternFuncArgument> &args = f.extern_arguments();
        for (const ExternFuncArgument &arg : args) {
            if (arg.is_expr()) {
                extern_call_args.push_back(arg.expr);
            } else if (arg.is_func()) {
                Function input(arg.func);
                LoopLevel store_level = input.schedule().store_level().lock();
                LoopLevel compute_level = input.schedule().compute_level().lock();
                if (store_level == compute_level) {
                    for (int k = 0; k < input.outputs(); k++) {
                        string buf_name = input.name();
                        if (input.outputs() > 1) {
                            buf_name += "." + std::to_string(k);
                        }
                        buf_name += ".buffer";
                        Expr buffer = Variable::make(type_of<struct halide_buffer_t *>(), buf_name);
                        extern_call_args.push_back(buffer);
                    }
                } else {
                    for (int k = 0; k < input.outputs(); k++) {
                        string buf_name = input.name() + "." + std::to_string(k) + ".tmp_buffer";
                        extern_call_args.push_back(Variable::make(type_of<struct halide_buffer_t *>(), buf_name));
                    }
                }
            } else if (arg.is_buffer()) {
                Buffer<> b = arg.buffer;
                Parameter p(b.type(), true, b.dimensions(), b.name());
                p.set_buffer(b);
                Expr buf = Variable::make(type_of<struct halide_buffer_t *>(), b.name() + ".buffer", p);
                extern_call_args.push_back(buf);
            } else if (arg.is_image_param()) {
                Parameter p = arg.image_param;
                Expr buf = Variable::make(type_of<struct halide_buffer_t *>(), p.name() + ".buffer", p);
                extern_call_args.push_back(buf);
            } else {
                internal_error << "Bad ExternFuncArgument type\n";
            }
        }
        Expr expr = f.make_call_to_extern_definition(extern_call_args, get_target_from_environment());
        stream << expr;
    } else {
        if (f.values().size() > 1) {
            stream << "{";
        }
        for (int i = 0; i < (int)f.values().size(); ++i) {
            stream << f.values()[i];
            if (i != (int)f.values().size()-1) {
                stream << ", ";
            }
        }
        if (f.values().size() > 1) {
            stream << "}";
        }
    }
    return stream.str();
}

/** Return all pairs of functions which operation only involves pointwise copy
  * of another function and the function from which it copies from. Ignore
  * functions that have updates or are extern functions. */
map<string, string> get_pointwise_copies(const map<string, Function> &env) {

    /*debug(4) << "\n\nGET POINTWISE COPIES:\n";
    for (const auto &iter : env) {
        debug(4) << "\t" << print_function(iter.second) << "\n";
    }
    debug(4) << "\n\n";*/

    // We should only consider the case when the function only has 1 caller
    map<string, int> num_callers;
    set<string> inlined;
    for (const auto &caller : env) {
        debug(4) << "...Function: " << caller.first << ", inlined? "
                 << caller.second.schedule().compute_level().is_inlined()
                 << ", can be inlined? " << caller.second.can_be_inlined() << "\n";
        if (caller.second.can_be_inlined() &&
            caller.second.schedule().compute_level().is_inlined()) {
            inlined.insert(caller.first);
        }
        for (const auto &callee : find_direct_calls(caller.second)) {
            if (callee.first != caller.first) {
                debug(4) << "\t\tadding callee: " << callee.first << "\n";
                num_callers[callee.first] += 1;
            } else {
                debug(4) << "\t\tignoring self callee: " << callee.first << "\n";
            }
        }
    }

    debug(4) << "\n\nINLINED FUNCTIONS: {";
    for (const auto &s : inlined) {
        debug(4) << s << ", ";
    }
    debug(4) << "}\n\n";

    /*debug(4) << "\n\nNUM CALLERS:\n";
    for (const auto &iter : num_callers) {
        debug(4) << "\t" << iter.first << ": " << iter.second << "\n";
    }
    debug(4) << "\n\n";*/

    // TODO(psuriana): Need to figure out that the copies are on the same device;
    // otherwise, it shouldn't have been optimized away

    map<string, string> pointwise_copies;
    for (const auto &iter : env) {
        // Ignore inlined function
        // TODO(psuriana): how should we handle the case when either the producer
        // or the consumer of the copy-pair is inlined?
        if (inlined.count(iter.first)) {
            debug(4) << "...skipping checking " << iter.first << " since it is inlined\n";
            continue;
        }
        string copied_from = get_pointwise_copy_producer(iter.second, env, num_callers, inlined); // Producer's name
        if (!copied_from.empty()) {
            pointwise_copies.emplace(iter.first, copied_from);
        }
    }

    // TODO(psuriana): Need to simplify copy-chaining
    // TODO(psuriana): How do you handle the chaining case which involves
    // halide_buffer_copy?
    debug(4) << "\n\nTRY SIMPLIFY COPY CHAINING:\n";
    bool fixed = false;
    while (!fixed) {
        fixed = true;
        for (auto iter = pointwise_copies.begin(); iter != pointwise_copies.end(); ++iter) {
            debug(4) << "...Checking cons: " << iter->first << ", prod: " << iter->second << "\n";

            auto other = pointwise_copies.find(iter->second);
            if (other != pointwise_copies.end()) {
                iter->second = other->second;
                debug(4) << "\t\tfind (erasing) chain cons: " << other->first << ", prod: " << other->second << "\n";
                pointwise_copies.erase(other);
                internal_assert(!pointwise_copies.count(iter->second)) << iter->second << " still in TMP\n";
                fixed = true;
            }
        }
    }
    debug(4) << "\n\n";
    return pointwise_copies;
}

void copy_elision_test() {
    if (1) {
        Func tile("tile"), output("output"), f("f"), g("g"), h("h"), in("in");
        Var x("x"), y("y");

        f(x, y) = x + y;
        g(x, y) = x - y;
        h(x, y) = g(x, y);
        in(x, y) = h(x, y);
        tile(x, y) = {f(x, y), g(x, y)};
        output(x, y) = tile(y, x);

        map<string, Function> env;
        env.emplace(tile.name(), tile.function());
        env.emplace(output.name(), output.function());
        env.emplace(f.name(), f.function());
        env.emplace(g.name(), g.function());
        env.emplace(h.name(), h.function());
        env.emplace(in.name(), in.function());

        f.compute_root();
        g.compute_root();
        h.compute_root();
        in.compute_root();
        tile.compute_root();

        for (auto &iter : env) {
           iter.second.lock_loop_levels();
        }

        map<string, string> result = get_pointwise_copies(env);
        debug(0) << "\nPointwise copies:\n";
        for (const auto &p : result) {
            debug(0) << "cons: " << p.first << " -> prod: " << p.second << "\n";
            debug(0) << "\t\tcons: " << print_function(env.at(p.first)) << "\n";
            debug(0) << "\t\tprod: " << print_function(env.at(p.second)) << "\n\n";
        }
        debug(0) << "\n";
    }

    if (1) {
        Func input("input"), input_copy("input_copy"), work("work"), output("output"), output_copy("output_copy"), g("g");
        Var x("x"), y("y");

        input(x, y) = x + y;
        input_copy(x, y) = input(x, y);
        work(x, y) = input_copy(x, y) * 2;
        output(x, y) = work(x, y);
        output_copy(x, y) = output(x, y);

        output.copy_to_device();

        map<string, Function> env;
        env.emplace(input.name(), input.function());
        env.emplace(input_copy.name(), input_copy.function());
        env.emplace(work.name(), work.function());
        env.emplace(output.name(), output.function());
        env.emplace(output_copy.name(), output_copy.function());

        input.compute_root();
        input_copy.compute_root();
        work.compute_root();
        output.compute_root();

        for (auto &iter : env) {
           iter.second.lock_loop_levels();
        }

        map<string, string> result = get_pointwise_copies(env);
        debug(0) << "\nPointwise copies:\n";
        for (const auto &p : result) {
            debug(0) << "cons: " << p.first << " -> prod: " << p.second << "\n";
            debug(0) << "\t\tcons: " << print_function(env.at(p.first)) << "\n";
            debug(0) << "\t\tprod: " << print_function(env.at(p.second)) << "\n\n";
        }
        debug(0) << "\n";
    }

    if (1) {
        Func input("input"), input_copy("input_copy"), work("work");
        Func output("output"), output_copy("output_copy");

        Var x("x"), y("y");

        input(x, y) = x + y;
        input_copy(x, y) = input(x, y);
        work(x, y) = input_copy(x, y) * 2;
        output(x, y) = work(x, y);
        output_copy(x, y) = output(x, y);

        map<string, Function> env;
        env.emplace(input.name(), input.function());
        env.emplace(input_copy.name(), input_copy.function());
        env.emplace(work.name(), work.function());
        env.emplace(output.name(), output.function());
        env.emplace(output_copy.name(), output_copy.function());

        input.compute_root();
        input_copy.compute_root();
        work.compute_root();
        output.compute_root();

        for (auto &iter : env) {
           iter.second.lock_loop_levels();
        }

        map<string, string> result = get_pointwise_copies(env);
        debug(0) << "\nPointwise copies:\n";
        for (const auto &p : result) {
            debug(0) << "cons: " << p.first << " -> prod: " << p.second << "\n";
            debug(0) << "\t\tcons: " << print_function(env.at(p.first)) << "\n";
            debug(0) << "\t\tprod: " << print_function(env.at(p.second)) << "\n\n";
        }
        debug(0) << "\n";
    }


    std::cout << "Copy elision test passed" << std::endl;
}

}  // namespace Internal
}  // namespace Halide
