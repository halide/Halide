using namespace Halide;
#include <Halide.h>
#ifdef NOSTDOUT
#define OFILE_AS "x.s"
#else
#define OFILE_AS "/dev/stdout"
#endif
#define OFILE_BC "x.bc"

#define COMPILE(X, Y)  ((X).compile_to_assembly(OFILE_AS, args, Y, target))
#define COMPILE_BC(X, Y)  ((X).compile_to_bitcode(OFILE_BC, args, Y, target))

void setupHexagonTarget(Target &target) {
        target.os = Target::OSUnknown; // The operating system
        target.arch = Target::Hexagon;   // The CPU architecture
        target.bits = 32;            // The bit-width of the architecture
        target.set_feature(Target::HVX);
        target.set_feature(Target::NoAsserts);
        target.set_feature(Target::NoBoundsQuery);
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
void set_stride_multiple(ImageParam &I, int dim, int m) {
  I.set_stride_multiple(dim, m);
}
void set_stride_multiple(Func &f, int dim, int m) {
  Expr stride = f.output_buffer().stride(dim);
  f.output_buffer().set_stride_multiple(dim, m);
}
