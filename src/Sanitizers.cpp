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
        if (op->is_producer) {
            Stmt body = mutate(op->body);

            Expr buffer = Variable::make(type_of<struct buffer_t *>(), op->name + ".buffer");
            Stmt annotate = call_extern_and_assert("halide_msan_annotate_buffer_is_initialized", {buffer});

            body = Block::make(body, annotate);
            stmt = ProducerConsumer::make(op->name, op->is_producer, body);
        } else {
            IRMutator::visit(op);
        }
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
