#ifndef HALIDE_INTERNAL_EXPR_INTERPRETER_H
#define HALIDE_INTERNAL_EXPR_INTERPRETER_H

#include <Halide.h>

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace Halide {
namespace Internal {

class ExprInterpreter : public IRVisitor {
public:
    using Scalar = std::variant<int64_t, uint64_t, double>;

    struct EvalValue {
        Type type;
        std::vector<Scalar> lanes;

        EvalValue() = default;
        explicit EvalValue(Type t);
    };

    std::map<std::string, EvalValue> var_env;
    EvalValue result;

    EvalValue eval(const Expr &e);

protected:
    using IRVisitor::visit;
    void truncate(EvalValue &v);

    void visit(const IntImm *op) override;
    void visit(const UIntImm *op) override;
    void visit(const FloatImm *op) override;
    void visit(const StringImm *op) override;
    void visit(const Variable *op) override;
    void visit(const Cast *op) override;
    void visit(const Reinterpret *op) override;
    void visit(const Add *op) override;
    void visit(const Sub *op) override;
    void visit(const Mul *op) override;
    void visit(const Div *op) override;
    void visit(const Mod *op) override;
    void visit(const Min *op) override;
    void visit(const Max *op) override;
    void visit(const EQ *op) override;
    void visit(const NE *op) override;
    void visit(const LT *op) override;
    void visit(const LE *op) override;
    void visit(const GT *op) override;
    void visit(const GE *op) override;
    void visit(const And *op) override;
    void visit(const Or *op) override;
    void visit(const Not *op) override;
    void visit(const Select *op) override;
    void visit(const Load *op) override;
    void visit(const Ramp *op) override;
    void visit(const Broadcast *op) override;
    void visit(const Call *op) override;
    void visit(const Shuffle *op) override;
    void visit(const VectorReduce *op) override;
    void visit(const Let *op) override;

private:
    template<typename F>
    EvalValue apply_unary(Type t, const EvalValue &a, F f);

    template<typename F>
    EvalValue apply_binary(Type t, const EvalValue &a, const EvalValue &b, F f);

    template<typename F>
    EvalValue apply_cmp(Type t, const EvalValue &a, const EvalValue &b, F f);

public:
    static void test();
};

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_INTERNAL_EXPR_INTERPRETER_H
