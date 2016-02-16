#include <Halide.h>
#include "halide-hexagon-setup.h"
#include "sat_arith.h"
#include <stdio.h>
using namespace Halide;
using namespace Halide::Internal;
IRPrinter irp(std::cerr);

// RUN: ./sat_arith.out | FileCheck %s

int main(int argc, char **argv) {
  Target target;
  setupHexagonTarget(target, Target::HVX_64);
  commonTestSetup(target);
  // CHECK: vsub(v{{[0-9]+}}.ub,v{{[0-9]+}}.ub):sat
  SatSub<uint8_t>(target);
  // FIXME: The reason we don't do SatSub<int8_t>(target) is that there is no
  // way to saturate and pack shorts (signed or unsigned) into signed bytes.
  // Assuming 'a' and 'b' were, say, i8x64 types, we'd be doing exactly this,
  // i.e. saturating and packing the widened result of the subtract (i16x64)
  // into signed bytes. This isn't supported. Should we be warning here?
  // At the present moment, the compilers asserts with the following message.
  //        Saturate and packing not supported when downcasting shorts
  //        (signed and unsigned) to signed chars.
  //         Aborted
  // SatSub<int8_t>(target);

  // CHECK: vsub(v{{[0-9]+}}.uh,v{{[0-9]+}}.uh):sat
  SatSub<uint16_t>(target);

  // CHECK: vsub(v{{[0-9]+}}.h,v{{[0-9]+}}.h):sat
  SatSub<int16_t>(target);

  // CHECK: vsub(v{{[0-9]+}}.w,v{{[0-9]+}}.w):sat
  SatSub<int32_t>(target);

  // CHECK: vadd(v{{[0-9]+}}.ub,v{{[0-9]+}}.ub):sat
  SatAdd<uint8_t>(target);
  // See FIXME comment above about SatSub<int8_t>(target);
  // SatAdd<int8_t>(target);

  // CHECK: vadd(v{{[0-9]+}}.uh,v{{[0-9]+}}.uh):sat
  SatAdd<uint16_t>(target);

  // CHECK: vadd(v{{[0-9]+}}.h,v{{[0-9]+}}.h):sat
  SatAdd<int16_t>(target);

  // CHECK: vadd(v{{[0-9]+}}.w,v{{[0-9]+}}.w):sat
  SatAdd<int32_t>(target);
  printf ("Done\n");
  return 0;
}
