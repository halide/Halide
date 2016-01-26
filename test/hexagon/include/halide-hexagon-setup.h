#ifndef __HALIDE_HEXAGON_SETUP_H
#define __HALIDE_HEXAGON_SETUP_H

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
void set_stride_multiple(ImageParam &I, int dim, int m) {
  I.set_stride_multiple(dim, m);
}
void set_stride_multiple(Func &f, int dim, int m) {
  Expr stride = f.output_buffer().stride(dim);
  f.output_buffer().set_stride_multiple(dim, m);
}

Expr sat_8(Expr e) {
  return clamp(e, -128, 127);
}

Expr usat_8(Expr e) {
  return clamp(e, 0, 255);
}

Expr saturating_subtract(Expr a, Expr b) {
  Type wider = Int(a.type().bits() * 2);
  /* FIXME: There is a problem here that I do not already know how to address.
    There is no way to saturate and pack shorts (signed or unsigned) into signed
    bytes. Assuming 'a' and 'b' were, say, i8x64 types, we'd be doing exactly
    this, i.e. saturating and packing the widened result of the subtract
    (i16x64) into signed bytes. This isn't supported. Should we be warning here?
    At the present moment, the compilers asserts with the following message.
           Saturate and packing not supported when downcasting shorts
           (signed and unsigned) to signed chars.
            Aborted
  */
  return cast(a.type(), clamp(cast(wider, a) - cast(wider, b),
                              a.type().min(), a.type().max()));
}
Expr saturating_add(Expr a, Expr b) {
  Type wider = Int(a.type().bits() * 2);
  /* FIXME: See comment in saturating_subtract above.  */
  return cast(a.type(), clamp(cast(wider, a) + cast(wider, b),
                              a.type().min(), a.type().max()));
}

#endif
