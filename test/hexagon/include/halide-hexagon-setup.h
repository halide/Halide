using namespace Halide;
#include <Halide.h>
#include <assert.h>
#ifdef NOSTDOUT
#define OFILE_AS "x.s"
#else
#define OFILE_AS "/dev/stdout"
#endif
#define OFILE_BC "x.bc"

#define COMPILE(X, Y)  ((X).compile_to_assembly(OFILE_AS, args, Y, target))
#define COMPILE_BC(X, Y)  ((X).compile_to_bitcode(OFILE_BC, args, Y, target))

void disableBounds(Target &target) {
    target.set_feature(Target::NoBoundsQuery);
}

void disableAsserts(Target &target) {
    target.set_feature(Target::NoAsserts);
}

void commonTestSetup(Target &target) {
    disableAsserts(target);
}

void commonPerfSetup(Target &target) {
    disableAsserts(target);
    disableBounds(target);
}
void setupHVXSize(Target &target, Target::Feature F)  {
    if (F == Target::HVX_128) {
      target.set_feature(Target::HVX_128);
      target = target.without_feature(Target::HVX_64);
    } else if (F == Target::HVX_64) {
      target.set_feature(Target::HVX_64);
      target = target.without_feature(Target::HVX_128);
    } else {
      fprintf(stderr, "Bad Target vec size feature\n");
      assert(0);
    }
}

void setupHexagonTarget(Target &target, Target::Feature F=Target::HVX_128) {
        target.os = Target::HexagonStandalone; // The operating system
        target.arch = Target::Hexagon;   // The CPU architecture
        target.bits = 32;            // The bit-width of the architecture
        setupHVXSize(target, F);
}

Expr sat_i32(Expr e) {
  int max = 0x7fffffff;
  int min = 0x80000000;
  return clamp(e, min, max);
}
void set_min(ImageParam &I, int dim, Expr a) {
  I.set_min(dim, a);
}
void set_output_buffer_min(Func &f, int dim, Expr a) {
  f.output_buffer().set_min(dim, a);
}
void set_stride_multiple(OutputImageParam I, int dim, int m) {
  I.set_stride(dim, (I.stride(dim) / m) * m);
}
void set_stride_multiple(Func f, int dim, int m) {
  set_stride_multiple(f.output_buffer(), dim, m);
}

Expr sat_8(Expr e) {
  return clamp(e, -128, 127);
}

Expr usat_8(Expr e) {
  return clamp(e, 0, 255);
}
