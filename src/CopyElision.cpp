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

    if (f.has_update_definition() || f.has_extern_definition() || inlined.count(f.name())) {
        return "";
    }

    const vector<Expr> &f_args = f.definition().args();

    string prod;
    for (int i = 0; i < (int)f.values().size(); ++i) {
        Expr val = perform_inline(f.values()[i], env, inlined);
        if (const Call *call = val.as<Call>()) {
            if (call->call_type == Call::Halide) {
                // Check if it is a pointwise copy. For tuple, check if 'f'
                // copies the whole tuple values.
                if (!prod.empty() && (prod != call->name)) {
                    debug(4) << "...Function \"" << f.name() << "\" calls multiple "
                             << "functions: \"" << prod << "\" and \""
                             << call->name << "\"\n";
                    return "";
                }
                prod = call->name;

                // TODO(psuriana): How should we handle the case when 'prod'
                // is scheduled to be inlined but not exactly inline-able
                // (i.e. have update definition or specialization)?
                if (env.at(prod).schedule().compute_level().is_inlined()) {
                    debug(4) << "...Function \"" << f.name() << "\" calls \"inlined\" "
                             << "function: \"" << prod << "\"\n";
                    return "";
                }

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
                internal_assert(f_args.size() == call->args.size());

                // TODO(psuriana): If this is a halide_buffer_copy, we can't
                // simply compare the values() size since it is empty (right
                // now we don't need to worry about this case, since
                // we always return if the function has extern definition).
                // Should we ignore halide_buffer_copy or not?
                if (f.outputs() != prod_f.outputs()) {
                    debug(4) << "...Function \"" << f.name() << "\" does not call "
                             << "the whole tuple values of function \""
                             << prod_f.name() << "\"(" << f.outputs()
                             << " vs " << prod_f.outputs() << ")\n"
                             << "\tcons -> " << print_function(f) << "\n"
                             << "\tprod -> " << print_function(prod_f) << "\n";
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
                    if (!equal(f_args[j], call->args[j])) {
                        debug(4) << "At arg " << j << ", " << f.name() << " (arg: "
                                 << f_args[i] << ") != " << prod_f.name()
                                 << "[" << call->value_index << "] (arg: "
                                 << call->args[j] << ")\n";
                        return "";
                    }
                }
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

map<string, string> get_valid_copy_elision_pairs(const map<string, Function> &env) {

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

    /*debug(0) << "\n\nINLINED FUNCTIONS: {";
    for (const auto &s : inlined) {
        debug(0) << s << ", ";
    }
    debug(0) << "}\n\n";*/

    map<string, string> pointwise_copies;
    for (const auto &iter : env) {
        // Ignore inlined functions
        // TODO(psuriana): how should we handle the case when either the producer
        // or the consumer of the copy-pair is inlined?
        string copied_from = get_pointwise_copy_producer(iter.second, env, num_callers, inlined);
        if (!copied_from.empty()) {
            pointwise_copies.emplace(iter.first, copied_from);
        }
    }

    /*debug(0) << "\nBEFORE Pointwise copies:\n";
    for (const auto &p : pointwise_copies) {
        debug(0) << "cons: " << p.first << " -> prod: " << p.second << "\n";
        debug(0) << "\t\tcons: " << print_function(env.at(p.first)) << "\n";
        debug(0) << "\t\tprod: " << print_function(env.at(p.second)) << "\n\n";
    }
    debug(0) << "\n\n";*/

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
                //pointwise_copies.erase(other);
                other->second = "";
                //internal_assert(!pointwise_copies.count(iter->second)) << iter->second << " still in TMP\n";
                fixed = true;
            }
        }
    }
    debug(4) << "\n\n";

    /*debug(0) << "\nAFTER Pointwise copies:\n";
    for (const auto &p : pointwise_copies) {
        debug(0) << "cons: " << p.first << " -> prod: " << p.second << "\n";
        debug(0) << "\t\tcons: " << print_function(env.at(p.first)) << "\n";
        if (p.second.empty()) {
            debug(0) << "\t\tprod: NONE\n\n";
        } else {
            debug(0) << "\t\tprod: " << print_function(env.at(p.second)) << "\n\n";
        }
    }
    debug(0) << "\n\n";*/
    return pointwise_copies;
}

}  // namespace Internal
}  // namespace Halide
