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
  bool is_double;

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

  HexagonIRChecker(bool has_double) : is_double(has_double) {}

private:
  bool is_unsupported_type(Type t, HexagonIRChecker::HVX_mode m ) {
    if (!t.is_vector())
      return false;
    int vec_size_in_bits = t.bits() * t.lanes();
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
    if (t.bits() * t.lanes() == 4 * hexagon_vector_size_bits(m))
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
    if (!is_double &&
        is_unsupported_type(op->type, HexagonIRChecker::Single)) {
      user_warning << "Unsupported type (Single mode): " << op->type << " in " << (Expr) op << "\n";
    }
    if (is_double &&
        is_unsupported_type(op->type, HexagonIRChecker::Double)) {
      user_warning << "Unsupported type (Double mode): " << op->type << " in " << (Expr) op << "\n";
    }
    if (!is_double &&
        is_vector_quad(op->type, HexagonIRChecker::Single)) {
      user_warning << "Vector Quad (Single mode): " << op->type << " in " << (Expr) op << "\n";
    }
    if (is_double &&
        is_vector_quad(op->type, HexagonIRChecker::Double)) {
      user_warning << "Vector Quad (Double mode): " << op->type << " in " << (Expr) op << "\n";
    }
    return op;
  }
};

Stmt hexagon_ir_checker(Stmt s, const Target &t) {
    bool has_double = t.has_feature(Target::HVX_128);
    return HexagonIRChecker(has_double).mutate(s);
}

}

}
