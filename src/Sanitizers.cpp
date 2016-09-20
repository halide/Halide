#include "Sanitizers.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

namespace {

class InjectMSANHelpers : public IRMutator {

    void visit(const ProducerConsumer *op) {
        Stmt produce, update, consume;

        produce = mutate(op->produce);
        if (op->update.defined()) {
            update = mutate(op->update);
        }
        consume = mutate(op->consume);

        std::string var_name = op->name + ".buffer";
        debug(3) << "Annotating buffer " << var_name << "\n";
        Expr buffer = Variable::make(type_of<struct buffer_t *>(), var_name);
        // Return type is really 'void', but no way to represent that in our IR.
        // Precedent (from halide_print, etc) is to use Int(32) and ignore the result.
        Expr call = Call::make(Int(32), "halide_msan_annotate_buffer_is_initialized", {buffer}, Call::Extern);
        Stmt annotate = Evaluate::make(call);

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
