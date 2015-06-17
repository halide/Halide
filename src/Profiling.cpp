#include <algorithm>
#include <map>
#include <string>
#include <limits>

#include "Profiling.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

class InjectProfiling : public IRMutator {
public:
    map<string, int> indices;   // maps from func name -> index in buffer.

    InjectProfiling() {
        indices["overhead"] = 0;
    }

private:
    using IRMutator::visit;

    void visit(const ProducerConsumer *op) {
        int idx;
        map<string, int>::iterator iter = indices.find(op->name);
        if (iter == indices.end()) {
            idx = (int)indices.size();
            indices[op->name] = idx;
        }

        Stmt produce = mutate(op->produce);
        Stmt update = op->update.defined() ? mutate(op->update) : Stmt();
        Stmt consume = mutate(op->consume);

        Expr profiler_token = Variable::make(Int(32), "profiler_token");
        Expr profiler_state = Variable::make(Handle(), "profiler_state");

        // This call gets inlined and becomes a single store instruction.
        Expr set_task = Call::make(Int(32), "halide_profiler_set_current_func",
                                   {profiler_state, profiler_token, idx}, Call::Extern);

        produce = Block::make(Evaluate::make(set_task), produce);

        stmt = ProducerConsumer::make(op->name, produce, update, consume);
    }
};

Stmt inject_profiling(Stmt s, string pipeline_name) {
    InjectProfiling profiling;
    s = profiling.mutate(s);

    int num_funcs = (int)(profiling.indices.size());

    Expr func_names_buf = Load::make(Handle(), "profiling_func_names", 0, Buffer(), Parameter());
    func_names_buf = Call::make(Handle(), Call::address_of, {func_names_buf}, Call::Intrinsic);

    Expr start_profiler = Call::make(Int(32), "halide_profiler_pipeline_start",
                                     {pipeline_name, num_funcs, func_names_buf}, Call::Extern);

    Expr get_state = Call::make(Handle(), "halide_profiler_get_state", {}, Call::Extern);

    Expr profiler_token = Variable::make(Int(32), "profiler_token");

    Expr stop_profiler = Call::make(Int(32), "halide_profiler_pipeline_end",
                                    {profiler_token}, Call::Extern);

    s = Block::make(s, Evaluate::make(stop_profiler));

    s = LetStmt::make("profiler_state", get_state, s);
    s = LetStmt::make("profiler_token", start_profiler, s);

    for (std::pair<string, int> p : profiling.indices) {
        s = Block::make(Store::make("profiling_func_names", p.first, p.second), s);
    }

    s = Allocate::make("profiling_func_names", Handle(), {num_funcs}, const_true(), s);

    return s;
}

}
}
