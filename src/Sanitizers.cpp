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

    void visit(const Call *op) {
        // convert all calls to halide_copy_to_host() into a pair of calls:
        // halide_copy_to_host() + halide_msan_annotate_buffer_is_initialized()
        if (op->call_type == Call::Extern && op->name == "halide_copy_to_host") {
            Expr call_copy_to_host = op;
            Expr call_annotate = Call::make(Int(32), "halide_msan_annotate_buffer_is_initialized", op->args, Call::Extern);
            std::string copy_to_host_result_name = unique_name("copy_to_host_result");
            Expr copy_to_host_result = Variable::make(Int(32), copy_to_host_result_name);
            expr = Let::make(copy_to_host_result_name, call_copy_to_host, 
                             select(copy_to_host_result != 0, copy_to_host_result, call_annotate));

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
