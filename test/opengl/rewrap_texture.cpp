// Test doesn't build on windows, because OpenGL on windows is a nightmare.
#ifdef _WIN32
#include <stdio.h>
int main() {
    printf("Skipping test on Windows\n");
    return 0;
}
#else

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "Halide.h"
#include "HalideRuntimeOpenGL.h"

#include "runtime/mini_opengl.h"

using namespace Halide;

extern "C" void glGenTextures(GLsizei, GLuint *);
extern "C" void glTexParameteri(GLenum, GLenum, GLint);
extern "C" void glBindTexture(GLenum, GLuint);
extern "C" void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid *);

int main() {

    // This test must be run with an OpenGL target.
    const Target target = get_jit_target_from_environment().with_feature(Target::OpenGL);

    const int width = 255;
    const int height = 10;

    // Create with the interleaved storage order needed by GLSL
    const std::vector<int> glsl_order{2, 0, 1};
    Buffer<uint8_t> input({width, height, 3}, glsl_order);
    Buffer<uint8_t> out1({width, height, 3}, glsl_order);
    Buffer<uint8_t> out2({width, height, 3}, glsl_order);
    Buffer<uint8_t> out3({width, height, 3}, glsl_order);

    Var x, y, c;
    Func g;
    g(x, y, c) = input(x, y, c);
    g.bound(c, 0, 3);
    g.glsl(x, y, c);

    g.realize(out1, target);  // run once to initialize OpenGL

    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    // wrapping a texture should work
    out2.device_wrap_native(DeviceAPI::GLSL, texture_id, target);
    g.realize(out2, target);
    out2.device_detach_native();

    // re-wrapping the texture should not abort
    out3.device_wrap_native(DeviceAPI::GLSL, texture_id, target);
    g.realize(out3, target);
    out3.device_detach_native();

    printf("Success!\n");
    return 0;
}

#endif
