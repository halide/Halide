#include "Sanitizers.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

namespace {

Stmt call_extern_and_assert(const std::string& name, const std::vector<Expr>& args) {
    Expr call = Call::make(Int(32), name, args, Call::Extern);
    std::string call_result_name = unique_name(name + "_result");
    Expr call_result_var = Variable::make(Int(32), call_result_name);
    return LetStmt::make(call_result_name, call,
                         AssertStmt::make(EQ::make(call_result_var, 0), call_result_var));
}

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
        Stmt annotate = call_extern_and_assert("halide_msan_annotate_buffer_is_initialized", {buffer});

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
