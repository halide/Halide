#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "GL/glew.h"

#ifdef __APPLE__
#include "GLUT/glut.h"
#else
#include "GL/glut.h"
#endif

#include "GL/glx.h"

#include "../../src/buffer_t.h"

extern "C" void halide_opengl_dev_malloc(void* uctx, buffer_t* buf);
extern "C" void halide_opengl_dev_free(void* uctx, buffer_t* buf);
extern "C" void halide_opengl_init_kernels(void* uctx, const char* src, int size);
extern "C" void halide_opengl_dev_sync(void* uctx);
extern "C" void halide_opengl_release(void* uctx);
extern "C" void halide_opengl_copy_to_dev(void* uctx, buffer_t* buf);
extern "C" void halide_opengl_copy_to_host(void* uctx, buffer_t* buf);
extern "C" void halide_opengl_dev_run(
    void* user_context,
    const char* entry_name,
    int blocksX, int blocksY, int blocksZ,
    int threadsX, int threadsY, int threadsZ,
    int shared_mem_bytes,
    size_t arg_sizes[],
    void* args[]);


typedef void (*GLFUNC)();
extern "C" GLFUNC halide_opengl_get_proc_address(const char* name) {
    return glXGetProcAddressARB((const GLubyte*) name);
}


// Initializes OpenGL and sets up the OpenGL runtime in Halide.
static void Initialize(int *argc, char **argv) {
    glutInit(argc, argv);
    glutCreateWindow("Halide Test");
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        fprintf(stderr, "Error: Could not initialize GLEW\n");
        exit(1);
    }

    if (!GLEW_VERSION_2_0) {
        fprintf(stderr, "Error: OpenGL 2.0 support required\n");
        exit(1);
    }
}


static const char *test_kernel_src =
    "/// KERNEL test_kernel\n"
    "/// IN buffer input\n"
    "/// IN int xpos\n"
    "/// IN float parameter\n"
    "/// OUT int buffer\n"
    "uniform sampler2D input;\n"
    "uniform int xpos;\n"
    "uniform float parameter;\n"
    "void main() {\n"
    "    gl_FragColor = parameter * texture2D(input, vec2(xpos, 0));\n"
    "}\n"
    "/// KERNEL fill_read\n"
    "/// OUT float output\n"
    "void main() {\n"
    "    gl_FragColor = vec4(1.0f, 0, 0, 0);\n"
    "}\n"
    "/// KERNEL kernel2\n"
    "/// IN float parameter\n"
    "void main() {\n"
    "    gl_FragColor = vec4(0);\n"
    "}\n"
    ;


extern "C" void blur_filter();
extern "C" void ycc_filter();

void test_compiled_filter() {

}

void test_mockup() {

    // Create input buffer
    buffer_t inbuf;
    memset(&inbuf, 0, sizeof(inbuf));
    inbuf.extent[0] = 12;
    inbuf.extent[1] = 32;
    inbuf.extent[2] = 4;
    inbuf.elem_size = sizeof(uint8_t);
    inbuf.stride[0] = inbuf.extent[2];
    inbuf.stride[1] = inbuf.extent[0] * inbuf.stride[0];
    inbuf.stride[2] = 1;
    inbuf.host = (uint8_t*)
        malloc(inbuf.extent[0] * inbuf.extent[1] * inbuf.extent[2] *
               inbuf.elem_size);
    inbuf.host_dirty = true;

    halide_opengl_dev_malloc(NULL, &inbuf);
    halide_opengl_copy_to_dev(NULL, &inbuf);

    // Create output buffer
    buffer_t outbuf;
    memset(&outbuf, 0, sizeof(outbuf));
    outbuf.extent[0] = inbuf.extent[0];
    outbuf.extent[1] = inbuf.extent[1];
    outbuf.extent[2] = inbuf.extent[2];
    outbuf.elem_size = sizeof(uint8_t);
    outbuf.host = (uint8_t*)
        malloc(outbuf.extent[0] * outbuf.extent[1] * outbuf.extent[2] *
               outbuf.elem_size);
    halide_opengl_dev_malloc(NULL, &outbuf);

    // Create GLSL kernels
    halide_opengl_init_kernels(NULL, test_kernel_src, sizeof(test_kernel_src)-1);

    // Run one of the kernels
    int arg_xpos = 11;
    float arg_parameter = 3.14159f;
    void* args[] = {
        &inbuf.dev, &arg_xpos, &arg_parameter, &outbuf.dev, 0
    };
    size_t arg_sizes[] = {
        64, sizeof(arg_xpos)*4, sizeof(arg_parameter)*4, 64, 0
    };
    halide_opengl_dev_run(
        NULL,
        "test_kernel",
        1, 1, 1,                        // blocks
        outbuf.extent[0], outbuf.extent[1], outbuf.extent[2], // threads
        0,                              // shared_mem_bytes
        arg_sizes, args);

    // Copy back to host memory and check result
    halide_opengl_copy_to_host(NULL, &outbuf);
    // TODO: check result

    // Cleanup
    halide_opengl_dev_free(NULL, &inbuf);
    halide_opengl_dev_free(NULL, &outbuf);
//    halide_opengl_release(NULL);
}

int main(int argc, char* argv[]) {
    Initialize(&argc, argv);
    test_compiled_filter();
    test_mockup();
}
