#include <Halide.h>
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;
using Halide::Internal::Call;
using Halide::Internal::vec;

int main(int argc, char** argv) {

  // This test must be run with an OpenGL target
  const Target &target = get_jit_target_from_environment();
  if (!target.has_feature(Target::OpenGL))  {
    fprintf(stderr,"ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
    return 1;
  }

  // Create an input image
  Image<uint8_t> input(4, 4, 3);
  for (int y=0; y<input.height(); y++) {
    for (int x=0; x<input.width(); x++) {
      for (int c=0; c<3; c++) {
        input(x, y, c) = 10*x + y + c;
      }
    }
  }
  Var x("x"), y("y"), c("c");

  // Halide Call nodes with type Extern in a .glsl scheduled function do not
  // actually call a 'extern "C"' function. Instead they translate the arguments
  // passed from Halide types to GLSL types and create a GLSL function call to
  // the specified function name.

  Image<uint8_t> out(input.width(), input.height(), input.channels());

  // Call a scalar built-in function
  {
      Func step("step_extern");
      step(x) = Call::make(Float(32), "step",
                           vec<Expr>((float)input.width()/2, cast<float>(x)),
                           Halide::Internal::Call::Extern);


      // Define the test function expression:
      Func g;
      g(x, y, c) = cast<uint8_t>(step(x) * 255.0f);


      // Schedule the function for GLSL
      g.bound(c, 0, out.channels());
      g.glsl(x, y, c);
      g.realize(out);
      out.copy_to_host();

      // Check the output
      for (int y = 0; y < out.height(); y++) {
          for (int x = 0; x < out.width(); x++) {
              printf("%d,%d,%d ", out(x,y,0), out(x,y,1), out(x,y,2));
              for (int c = 0; c != out.channels(); c++) {
                  int expected = (x < out.width()/2) ? 0 : 255;
                  int result = out(x,y,c);
                  if (expected != result) {
                      printf("Error %d,%d,%d value %d should be %d",
                             x, y, c, result, expected);
                  }
              }
          }
          printf("\n");
      }
      printf("\n");

  }

#if 0
  // Using normalized texture coordinates via GLSL texture2d:
  {
      // GLSL ES 1.0 defines vec4 texture2d(sampler2d, vec2), we can pass an
      // ImageParam for the first argument, but there is no way to produce a
      // Float(32,2) directly. Instead, we define and call a helper function to
      // explicitly call the GLSL vec2 constructor, and pass this to the texture2d
      // extern.
      Func vec2("vec2_extern");
      vec2(x,y) = Call::make(Float(32,2), "vec2_extern",
                             vec<Expr>(x, y),
                             Halide::Internal::Call::Extern);

      Func texture2d("texure2d_extern");
      texture2d(x,y) = Call::make(Float(32,4), "texture2d",
                                  vec<Expr>(input.name(), vec2(x,y)),
                                  Halide::Internal::Call::Extern);

      Func shuffled("shuffled_extern");
      shuffled(x,y,c) = Call::make(Float(32), Call::shuffle_vector,
                                   vec<Expr>(texture2d(x,y),c),
                                   Halide::Internal::Call::Intrinsic);

      // Define the test function expression:
      Func g;
      g(x, y, c) = shuffled(x,y,c);

      Image<float> out(input.width()*2, input.height()*2, input.channels());

      // Schedule the function for GLSL
      g.bound(c, 0, out.channels());
      g.glsl(x, y, c);
      g.realize(out);
      out.copy_to_host();

      // Check the output
      for (int y=0; y<out.height(); y++) {
          for (int x=0; x<out.width(); x++) {
              printf("%d,%d,%d ",out(x,y,0),out(x,y,1),out(x,y,2));
          }
          printf("\n");
      }
      printf("\n");
  }
#endif

  return 0;
}