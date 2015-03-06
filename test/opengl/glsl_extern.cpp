#include <Halide.h>
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;
using Halide::Internal::Call;
using Halide::Internal::vec;


Expr vec2(Expr x, Expr y) {
  return Call::make(Float(32,2), "vec2",
                    vec<Expr>(x, y),
                    Call::Extern);
}

// GLSL ES 1.0 defines vec4 texture2D(sampler2d, vec2), we would like to pass an
// image as the first argument, but there is no way to convert an ImageParam
// into an expression in Halide without looking up a value in it. Instead, we
// call the glsl_inline intrinsic to output a string containing the image name
// at this point in the program output. Passing a StringImm instead of the image
// will cause the image argument to be simplified away during compilation, so if
// the image is not used elsewhere in the filter, we pass a non-static lookup to
// the image as the second argument to glsl_inline. The intrinisic only includes
// the first argument in the generated code.
//
// The next parameter to texture2D is a vec2. There is no way to produce a
// Float(32,2) directly. Instead, we define and call a helper function to
// explicitly call the GLSL vec2 constructor, and pass this to the texture2D
// extern.
//
// The value returned by texture2D is a vec4. This is represented in Halide as a
// Float(32,4). We extract a single channel from this type using the
// shuffle_vector intrinsic. Halide GLSL codegen will vectorize the shuffe
// vector intrinsic away if all of the channels are used in the expression.

template<typename ImageT>
Expr texture2D(ImageT input, Expr x, Expr y) {
  using Halide::Internal::Variable;
  using Halide::Internal::Parameter;
  std::string name = input.name();

  Type type = Buffer(input).type();
  Parameter param(type, true, 4, name);
  param.set_buffer(input);

  Expr v = Variable::make(Handle(),
                          name + ".buffer",
                          param);

  return Call::make(Float(32,4),
                    "texture2D",
                    vec<Expr>(v, vec2(x,y)),
                    Call::Extern);
}

Expr shuffle_vector(Expr v, Expr c) {
  return Call::make(Float(32), Call::shuffle_vector,
             vec<Expr>(v, c),
             Halide::Internal::Call::Intrinsic);
}


int main(int argc, char** argv) {

  // This test must be run with an OpenGL target
  const Target &target = get_jit_target_from_environment();
  if (!target.has_feature(Target::OpenGL))  {
    fprintf(stderr,"ERROR: This test must be run with an OpenGL target, e.g. by setting HL_JIT_TARGET=host-opengl.\n");
    return 1;
  }

  Var x("x"), y("y"), c("c");

  // Halide Call nodes with type Extern in a .glsl scheduled function do not
  // actually call a 'extern "C"' function. Instead they translate the arguments
  // passed from Halide types to GLSL types and create a GLSL function call to
  // the specified function name.


  // Call a scalar built-in function
  if (0) {
      int N = 4;
      Image<uint8_t> out(N, N, 3);

      Func step("step_extern");
      step(x) = Call::make(Float(32), "step",
                           vec<Expr>((float)N/2, cast<float>(x)),
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
              for (int c = 0; c != out.channels(); c++) {
                  int expected = (x < out.width()/2) ? 0 : 255;
                  int result = out(x,y,c);
                  if (expected != result) {
                      printf("Error %d,%d,%d value %d should be %d",
                             x, y, c, result, expected);
                  }
              }
          }
      }
  }

  // Using normalized texture coordinates via GLSL texture2d:
  {
      // Create an input image
      int N = 4;
      Image<float> input(N, N, 4);
      for (int y=0; y<input.height(); y++) {
        for (int x=0; x<input.width(); x++) {
          input(x, y, 0) = x;
          input(x, y, 1) = y;
          input(x, y, 2) = 0.0f;
          input(x, y, 3) = 0.0f;
        }
      }

      // Check the input
      for (int c = 0; c < input.channels(); ++c) {
          for (int y=0; y<input.height(); y++) {
              for (int x=0; x<input.width(); x++) {
                  printf("%f, ",input(x,y,c));
              }
              printf("\n");
          }
          printf("\n");
      }

      int M = 8;
      Image<float> out(M, M, 4);

      float offset = (1.0f/float(M)) * 0.5f;

      // Define the test function expression:
      Func g;
      g(x, y, c) = select(c == 0, cast<float>(x)/(float)(M) + offset,
                          c == 1, cast<float>(y)/(float)(M) + offset,
                          shuffle_vector(texture2D(input,
                                                   cast<float>(x)/(float)(M) + offset,
                                                   cast<float>(y)/(float)(M) + offset),
                                         c-2));

      // Schedule the function for GLSL
      g.bound(c, 0, out.channels());
      g.glsl(x, y, c);
      g.realize(out);
      out.copy_to_host();

      // Check the output
      for (int c = 0; c < out.channels(); ++c) {
          for (int y=0; y<out.height(); y++) {
              for (int x=0; x<out.width(); x++) {
                  printf("%f, ",out(x,y,c));
              }
              printf("\n");
          }
          printf("\n");
      }
  }

  return 0;
}