#include <algorithm>

#include "HexagonIRChecker.h"
#include "IRMutator.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

/////////////////////////////////////////////////////////////////////////////
class Hexagon_Lower : public IRMutator {
    using IRMutator::visit;
public:
    void visit(const For *op) {
        ForType for_type = op->for_type;

        // .parallel() schedule run-time support not yet implemented
        if (op->for_type == ForType::Parallel) {
            user_warning << "Lowering parallel loop\n";
            for_type = ForType::Serial;
            stmt = For::make(op->name, op->min, op->extent, for_type, op->device_api, op->body);
            return;
        }
        IRMutator::visit(op);
    }
};

Stmt hexagon_lower(Stmt s) {
    s = Hexagon_Lower().mutate(s);
    return s;
}

/////////////////////////////////////////////////////////////////////////////
class HexagonIRChecker  : public IRMutator {
  using IRMutator::visit;
public:
  enum HVX_mode {
    Single,
    Double
  };

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
  void visit(const Cast *cast_op) {
    expr = check_type (cast_op);
  }

private:
  bool is_invalid_type(Type t, HexagonIRChecker::HVX_mode m ) {
    if (!t.is_vector())
      return false;
    int vec_size_in_bits = t.bits * t.width;
    int hex_vec_size_in_bits = hexagon_vector_size_bits(m);
    if ((vec_size_in_bits != hex_vec_size_in_bits)
        && (vec_size_in_bits != 2 * hex_vec_size_in_bits)
        && (vec_size_in_bits != 4 * hex_vec_size_in_bits))
      return true;
    return false;
  }
  bool is_vector_quad(Type t, HexagonIRChecker::HVX_mode m) {
    if (!t.is_vector())
      return false;
    if (t.bits * t.width == 4 * hexagon_vector_size_bits(m))
      return true;
    return false;
  }
  int hexagon_vector_size_bits(HexagonIRChecker::HVX_mode m) {
    switch(m) {
    case HexagonIRChecker::Single: return 64 * 8;
    case HexagonIRChecker::Double: return 128 * 8;
    }
  }
  Expr check_type(const BaseExprNode *op) {
    if(is_invalid_type(op->type, HexagonIRChecker::Single)) {
      user_warning << "Invalid type:{SINGLE_MODE}: (" << op->type << "): " << (Expr) op << "\n";
    }
    if(is_invalid_type(op->type, HexagonIRChecker::Double)) {
      user_warning << "Invalid type:{DOUBLE_MODE}: (" << op->type << "): " << (Expr) op << "\n";
    }
    if(is_vector_quad(op->type, HexagonIRChecker::Single)) {
      user_warning << "Vector Quad:{SINGLE_MODE}: (" << op->type << "): " << (Expr) op << "\n";
    }
    if(is_vector_quad(op->type, HexagonIRChecker::Double)) {
      user_warning << "Vector Quad:{DOUBLE_MODE}: (" << op->type << "): " << (Expr) op << "\n";
    }
    return op;
  }
};

Stmt hexagon_ir_checker(Stmt s) {
    return HexagonIRChecker().mutate(s);
}

}

}
