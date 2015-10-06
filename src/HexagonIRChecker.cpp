#include <algorithm>

#include "HexagonIRChecker.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

class HexagonIRChecker  : public IRMutator {
  using IRMutator::visit;
  void visit(const EQ *cmp_op) {
    expr = check_type (cmp_op);
    return;
  }
  void visit(const NE *cmp_op) {
    expr = check_type (cmp_op);
    return;

  }
  void visit(const LT *cmp_op)  {
    expr = check_type (cmp_op);
    return;
  }
  void visit(const LE *cmp_op)  {
    expr = check_type (cmp_op);
    return;
  }
  void visit(const GT *cmp_op)  {
    expr = check_type (cmp_op);
    return;
  }
  void visit(const GE *cmp_op)  {
    expr = check_type (cmp_op);
    return;
  }
private:
  bool is_invalid_type(Type t) {
    if (!t.is_vector())
      return false;
    if (t.bits * t.width != hexagon_vector_size_bits())
      return true;
    return false;
  }
  int hexagon_vector_size_bits() {
    // Do double mode for now.
    return 128*8;
  }
  Expr check_type(const BaseExprNode *op) {
    if(is_invalid_type(op->type)) {
      user_warning << "Invalid type" << (Expr) op << "\n";
    }
    return op;
  }
};

Stmt hexagon_ir_checker(Stmt s) {
    return HexagonIRChecker().mutate(s);
}

}

}
