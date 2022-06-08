#include "ExprUsesVar.h"
#include "IRVisitor.h"

namespace Halide {
namespace Internal {

namespace {

class CountVarUses : public IRVisitor {
    std::map<std::string, int> &var_uses;

    void visit(const Variable *var) override {
        var_uses[var->name]++;
    }

    void visit(const Load *op) override {
        var_uses[op->name]++;
        IRVisitor::visit(op);
    }

    void visit(const Store *op) override {
        var_uses[op->name]++;
        IRVisitor::visit(op);
    }

    using IRVisitor::visit;

public:
    CountVarUses(std::map<std::string, int> &var_uses)
        : var_uses(var_uses) {
    }
};

}  // namespace

void count_var_uses(const Stmt &s, std::map<std::string, int> &var_uses) {
    CountVarUses counter(var_uses);
    s.accept(&counter);
}

void count_var_uses(const Expr &e, std::map<std::string, int> &var_uses) {
    CountVarUses counter(var_uses);
    e.accept(&counter);
}

}  // namespace Internal
}  // namespace Halide
