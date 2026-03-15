#ifndef HALIDE_IRGRAPHCXXPRINTER_H
#define HALIDE_IRGRAPHCXXPRINTER_H

#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "Expr.h"
#include "IR.h"
#include "IRVisitor.h"

namespace Halide {
namespace Internal {

class IRGraphCXXPrinter : public IRGraphVisitor {
public:
    std::ostream &os;

    // Tracks visited nodes so we don't print them twice (handles the DAG structure)
    std::map<const IRNode *, std::string> node_names;
    int var_counter = 0;

    IRGraphCXXPrinter(std::ostream &os) : os(os) {
    }

    void print(const Expr &e) {
        if (e.defined()) {
            e.accept(this);
        }
    }

    void print(const Stmt &s) {
        if (s.defined()) {
            s.accept(this);
        }
    }

private:
    template<typename T, typename... Args>
    void emit_node(const char *node_type_str, const T *op, Args &&...args);

    template<typename T>
    std::string to_cpp_arg(const T &x);

    template<typename T>
    std::string to_cpp_arg(const std::vector<T> &vec);

protected:
    void visit(const IntImm *) override;
    void visit(const UIntImm *) override;
    void visit(const FloatImm *) override;
    void visit(const StringImm *) override;
    void visit(const Cast *) override;
    void visit(const Reinterpret *) override;
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Mul *) override;
    void visit(const Div *) override;
    void visit(const Mod *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GT *) override;
    void visit(const GE *) override;
    void visit(const And *) override;
    void visit(const Or *) override;
    void visit(const Not *) override;
    void visit(const Select *) override;
    void visit(const Load *) override;
    void visit(const Ramp *) override;
    void visit(const Broadcast *) override;
    void visit(const Let *) override;
    void visit(const LetStmt *) override;
    void visit(const AssertStmt *) override;
    void visit(const ProducerConsumer *) override;
    void visit(const Store *) override;
    void visit(const Provide *) override;
    void visit(const Allocate *) override;
    void visit(const Free *) override;
    void visit(const Realize *) override;
    void visit(const Block *) override;
    void visit(const Fork *) override;
    void visit(const IfThenElse *) override;
    void visit(const Evaluate *) override;
    void visit(const Call *) override;
    void visit(const Variable *) override;
    void visit(const For *) override;
    void visit(const Acquire *) override;
    void visit(const Shuffle *) override;
    void visit(const Prefetch *) override;
    void visit(const HoistedStorage *) override;
    void visit(const Atomic *) override;
    void visit(const VectorReduce *) override;

public:
    static void test();
};

}  // namespace Internal

}  // namespace Halide

#endif  // HALIDE_IRGRAPHCXXPRINTER_H
