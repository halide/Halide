#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
  Func f;
  Var x;
  // Implicit variables only allowed when rhs is non-scaler.
  f(x, _) = 0;
  return 0;
}
