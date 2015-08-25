#include "Expr.h"
#include "llvm/Support/ErrorHandling.h"

namespace Halide {
namespace Internal {

FloatImm::HighestPrecisionTy FloatImm::as_highest_precision_float() const {
      if (const float16_t* asHalf = as<float16_t>()) {
          return (HighestPrecisionTy) *asHalf;
      }
      else if (const float* asFloat = as<float>()) {
          return (HighestPrecisionTy) *asFloat;
      }
      else if (const double* asDouble = as<double>()) {
          return *asDouble;
      }
      else {
          internal_error << "Float type not supported\n";
      }
      llvm_unreachable("float type not handled");
}

}
}
