#include "DebugToFile.h"
#include "IRMutator.h"
#include "Util.h"
#include "IROperator.h"
#include "Log.h"

#include <map>
#include <vector>
#include <sstream>

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

            // The name of the function
            args.push_back(new Call(Int(32), f.name(), vector<Expr>()));

            // The name of the file
            args.push_back(new Call(Int(32), f.debug_file(), vector<Expr>()));

            // The header           
            for (size_t i = 0; i < op->bounds.size(); i++) {
                if (i < 4) {
                    args.push_back(op->bounds[i].extent);
                } else {
                    args[3] = args[3] * op->bounds[i].extent;
                }
            }
            while (args.size() < 6) args.push_back(1);

            int type_code = 0;
            Type t = f.value().type();
            if (t == Float(32)) {
                type_code = 0;
            } else if (t == Float(64)) {
                type_code = 1;
            } else if (t == UInt(8)) {
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
                assert(false && "Type not supported for debug_to_file");
            }
            args.push_back(type_code);
            args.push_back(t.bits / 8);

            Expr call = new Call(Int(32), "debug to file", args);

            Stmt body = new AssertStmt(call == 0, 
                                       "Failed to dump function " + 
                                       f.name() + " to file " + f.debug_file());
            body = new Block(mutate(op->body), body);
        
            stmt = new Realize(op->name, op->type, op->bounds, body);

        } else {
            IRMutator::visit(op);
        }
    }

public:
    DebugToFile(const map<string, Function> &e) : env(e) {}
};

Stmt debug_to_file(Stmt s, const map<string, Function> &env) {
    return DebugToFile(env).mutate(s);
}

}
}
