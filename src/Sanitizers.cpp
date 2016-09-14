#include "Sanitizers.h"
#include "IRMutator.h"
#include "Debug.h"
#include "IRPrinter.h"
#include "CodeGen_GPU_Dev.h"
#include "IROperator.h"
#include <map>

namespace Halide {
namespace Internal {

namespace {

class InjectMSANHelpers : public IRMutator {
    using IRMutator::visit;

    void visit(const ProducerConsumer *op) {
        Stmt produce, update, consume;

        produce = mutate(op->produce);
        if (op->update.defined()) {
            update = mutate(op->update);
        }
        consume = mutate(op->consume);

        Expr buffer = Variable::make(type_of<struct buffer_t *>(), op->name + ".buffer");
        Expr call = Call::make(Int(32), "halide_msan_annotate_buffer_is_initialized", {buffer}, Call::Extern);
        std::string call_result_name = unique_name(op->name + "_annotate_result");
        Expr call_result_var = Variable::make(Int(32), call_result_name);
        Stmt annotate = LetStmt::make(call_result_name, call, AssertStmt::make(EQ::make(call_result_var, 0), call_result_var));

        if (op->update.defined()) {
            update = Block::make(update, annotate);
        } else {
            produce = Block::make(produce, annotate);
        }

        stmt = ProducerConsumer::make(op->name, produce, update, consume);
    }

public:
    InjectMSANHelpers() {}

};

}  // namespace

Stmt inject_msan_helpers(Stmt s) {
    return InjectMSANHelpers().mutate(s);
}

}
}
