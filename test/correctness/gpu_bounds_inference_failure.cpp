// from issue #3221

#include "Halide.h"

int main(int argc, char *argv[]) {

  Var x, y, p, d;

  Func f1, f2;
  f1(x, y, p) = 0;
  f2(x, y, p) = 0;

  RDom r(0, 10, 0, 10);

  Func b1, b2;
  b1(p) = 0.f;
  b1(p) += f1(r.x, r.y, p);

  b2(p) = 0.f;
  b2(p) += f2(r.x, r.y, p);

  Func d1, d2;
  d1(p) = b1(p) - b2(p);
  d2(p) = b2(p) - b1(p);

  Func result;
  result(d, p) = select(d == 0, d1(p), d2(p));

  d2.compute_root().gpu_blocks(p);

  // this used to cause an assertion error
  result.compile_jit(Target("host-cuda"));

  printf("Success!\n");
  return 0;
}
