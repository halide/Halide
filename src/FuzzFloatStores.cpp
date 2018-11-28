#include "FuzzFloatStores.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

namespace {
class FuzzFloatStores : public IRMutator2 {
    using IRMutator2::visit;

    Stmt visit(const Store *op) override {
        Type t = op->value.type();
        if (t.is_float()) {
            // Drop the last bit of the mantissa.
            Expr value = op->value;
            Expr mask = make_one(t.with_code(Type::UInt));
            value = reinterpret(mask.type(), value);
            value = value & ~mask;
            value = reinterpret(t, value);
            return Store::make(op->name, value, op->index, op->param, op->predicate);
        } else {
            return IRMutator2::visit(op);
        }
    }
};
}  // namespace

Stmt fuzz_float_stores(Stmt s) {
    return FuzzFloatStores().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
