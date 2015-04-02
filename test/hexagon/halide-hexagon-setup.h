using namespace Halide;
#include <Halide.h>
void setupHexagonTarget(Target &target) {
        target.os = Target::OSUnknown; // The operating system
        target.arch = Target::Hexagon;   // The CPU architecture
        target.bits = 32;            // The bit-width of the architecture
        target.set_feature(Target::HVX);
}

Expr sat_i32(Expr e) {
  int max = 0x7fffffff;
  int min = 0x80000000;
  return clamp(e, min, max);
}
