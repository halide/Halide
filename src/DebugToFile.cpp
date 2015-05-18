#include <map>
#include <vector>
#include <sstream>

#include "DebugToFile.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::vector;
using std::ostringstream;

class DebugToFile : public IRMutator {
    const map<string, Function> &env;

    using IRMutator::visit;

    void visit(const Realize *op) {
        map<string, Function>::const_iterator iter = env.find(op->name);
        if (iter != env.end() && !iter->second.debug_file().empty()) {
            Function f = iter->second;
            vector<Expr> args;

            user_assert(op->types.size() == 1)
                << "debug_to_file doesn't handle functions with multiple values yet\n";

            // The name of the file
            args.push_back(f.debug_file());

            // Inject loads to the corners of the function so that any
            // passes doing further analysis of buffer use understand
            // what we're doing (e.g. so we trigger a copy-back from a
            // device pointer).
            Expr num_elements = 1;
            for (size_t i = 0; i < op->bounds.size(); i++) {
                num_elements *= op->bounds[i].extent;
            }
            args.push_back(Load::make(op->types[0], f.name(), 0, Buffer(), Parameter()));
            args.push_back(Load::make(op->types[0], f.name(), num_elements-1, Buffer(), Parameter()));

            // The header
            for (size_t i = 0; i < op->bounds.size(); i++) {
                if (i < 4) {
                    args.push_back(op->bounds[i].extent);
                } else {
                    args.back() *= op->bounds[i].extent;
                }
            }
            // Fill the remaining args with ones.
            args.resize(7, 1);

            int type_code = 0;
            Type t = op->types[0];
            if (t == Float(32)) {
                type_code = 0;
            } else if (t == Float(64)) {
                type_code = 1;
            } else if (t == UInt(8) || t == UInt(1)) {
                type_code = 2;
            } else if (t == Int(8)) {
                type_code = 3;
            } else if (t == UInt(16)) {
                type_code = 4;
            } else if (t == Int(16)) {
                type_code = 5;
            } else if (t == UInt(32)) {
                type_code = 6;
            } else if (t == Int(32)) {
                type_code = 7;
            } else if (t == UInt(64)) {
                type_code = 8;
            } else if (t == Int(64)) {
                type_code = 9;
            } else {
                user_error << "Type " << t << " not supported for debug_to_file\n";
            }
            args.push_back(type_code);
            args.push_back(t.bytes());

            Expr call = Call::make(Int(32), Call::debug_to_file, args, Call::Intrinsic);
            string call_result_name = unique_name("debug_to_file_result");
            Expr call_result_var = Variable::make(Int(32), call_result_name);
            Stmt body = AssertStmt::make(call_result_var == 0,
                                         Call::make(Int(32), "halide_error_debug_to_file_failed",
                                                    vec<Expr>(f.name(), f.debug_file(), call_result_var),
                                                    Call::Extern));
            body = LetStmt::make(call_result_name, call, body);
            body = Block::make(mutate(op->body), body);

            stmt = Realize::make(op->name, op->types, op->bounds, op->condition, body);

        } else {
            IRMutator::visit(op);
        }
    }

public:
    DebugToFile(const map<string, Function> &e) : env(e) {}
};

Stmt debug_to_file(Stmt s, string output, const map<string, Function> &env) {
    // Temporarily wrap the statement in a realize node for the output function
    Function out = env.find(output)->second;
    std::vector<Range> output_bounds;
    for (int i = 0; i < out.dimensions(); i++) {
        string dim = int_to_string(i);
        Expr min    = Variable::make(Int(32), output + ".min." + dim);
        Expr extent = Variable::make(Int(32), output + ".extent." + dim);
        output_bounds.push_back(Range(min, extent));
    }
    s = Realize::make(output, out.output_types(), output_bounds, const_true(), s);
    s = DebugToFile(env).mutate(s);

    // Remove the realize node we wrapped around the output
    if (const Realize *r = s.as<Realize>()) {
        s = r->body;
    } else if (const Block *b = s.as<Block>()) {
        const Realize *r = b->rest.as<Realize>();
        internal_assert(r);
        s = Block::make(b->first, r->body);
    } else {
        internal_error << "Could not unwrap stmt after debug_to_file\n";
    }

    return s;
}

}
}
