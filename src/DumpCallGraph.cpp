#include "DumpCallGraph.h"

#include <map>
#include <cstdio>

#include "IR.h"
#include "Function.h"
#include "IRPrinter.h"

using std::map;
using std::string;

namespace Halide {
namespace Internal {

/* Find all the internal halide calls in an expr */
class FindAllCalls : public IRVisitor {
private:
    bool recursive;
public:
    FindAllCalls(bool recurse = false) : recursive(recurse) {}

    map<string, Function> calls;

    typedef map<string, Function>::iterator iterator;

    using IRVisitor::visit;

    void include_function(Function f) {
        iterator iter = calls.find(f.name());
        if (iter == calls.end()) {
            calls[f.name()] = f;
            if (recursive) {
                // recursively add everything called in the definition of f
                for (size_t i = 0; i < f.values().size(); i++) {
                    f.values()[i].accept(this);
                }
                // recursively add everything called in the definition of f's update step
                for (size_t i = 0; i < f.reduction_values().size(); i++) {
                    f.reduction_values()[i].accept(this);
                }
            }
        } else {
            assert(iter->second.same_as(f) &&
                   "Can't compile a pipeline using multiple functions with same name");
        }
    }

    void visit(const Call *call) {
        IRVisitor::visit(call);
        if (call->call_type == Call::Halide) {
            include_function(call->func);
        }
    }

    void dump_calls(FILE *of) {
        iterator it = calls.begin();
        while (it != calls.end()) {
            fprintf(of, "\"%s\"", it->first.c_str());
            ++it;
            if (it != calls.end()) {
                fprintf(of, ", ");
            }
        }
    }
};

}

using namespace Internal;

void dump_call_graph(const std::string &outfilename, Func &root) {
    FILE *of = fopen(outfilename.c_str(), "w");

    const Function &f = root.function();

    FindAllCalls all_calls(true);
    for (size_t i = 0; i < f.values().size(); i++) {
        f.values()[i].accept(&all_calls);
    }

    fprintf(of, "[\n");

    FindAllCalls::iterator it = all_calls.calls.begin();
    while (it != all_calls.calls.end()) {
        fprintf(of, " {\"name\": \"%s\", ", it->first.c_str());
        fprintf(of, "\"vars\": [");
        for (size_t i = 0; i < it->second.args().size(); i++) {
            fprintf(of, "\"%s\"", it->second.args()[i].c_str());
            if (i < it->second.args().size()-1) {
                fprintf(of, ", ");
            }
        }
        fprintf(of, "], ");

        fprintf(of, "\"calls\": [");
        FindAllCalls local_calls(false);
        for (size_t i = 0; i < it->second.values().size(); i++) {
            it->second.values()[i].accept(&local_calls);
        }
        local_calls.dump_calls(of);
        fprintf(of, "], ");

        // don't log reduction_value calls - these can't be meaningfully scheduled wrt. this function
        fprintf(of, "\"update_calls\": [");
        FindAllCalls update_calls(false);
        for (size_t i = 0; i < it->second.reduction_values().size(); i++) {
            it->second.reduction_values()[i].accept(&update_calls);
        }
        update_calls.dump_calls(of);
        fprintf(of, "]}");

        ++it;
        if (it != all_calls.calls.end()) {
            fprintf(of, ",\n");
        }
    }

    fprintf(of, "\n[\n");
    fclose(of);
}

}