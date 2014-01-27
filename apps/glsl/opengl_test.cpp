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
    "/// KERNEL fill_red\n"
    "/// OUT buffer output\n"
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

enum ImageLayout {
    Interleaved, Planar
};
void create_empty_image(buffer_t *buf, int w, int h, int c, int elem_size,
                        ImageLayout layout = Interleaved) {
    memset(buf, 0, sizeof(buffer_t));
    buf->extent[0] = w;
    buf->extent[1] = h;
    buf->extent[2] = c;
    buf->elem_size = elem_size;

    if (layout == Interleaved) {
        buf->stride[0] = buf->extent[2];
        buf->stride[1] = buf->extent[0] * buf->stride[0];
        buf->stride[2] = 1;
    } else {
        buf->stride[0] = 1;
        buf->stride[1] = buf->extent[0] * buf->stride[0];
        buf->stride[2] = buf->extent[1] * buf->stride[1];
    }
    size_t size = w * h * c * elem_size;
    buf->host = (uint8_t*)malloc(size);
    memset(buf->host, 0, size);
    buf->host_dirty = true;
}

void delete_image(buffer_t *buf) {
    halide_opengl_dev_free(NULL, buf);
    free(buf->host);
}

void test_copy() {
    bool success = true;
    const int W = 12, H = 32, C = 3;
    const uint8_t VAL = 0x1c;

    buffer_t buf;
    create_empty_image(&buf, W, H, C, sizeof(uint8_t), Planar);

    size_t size = W * H * C * sizeof(uint8_t);

    memset(buf.host, VAL, size);

    halide_opengl_dev_malloc(NULL, &buf);
    halide_opengl_copy_to_dev(NULL, &buf);

    memset(buf.host, 0, size);

    buf.dev_dirty = true;

    halide_opengl_copy_to_host(NULL, &buf);

    for (int i = 0; i < size; i++) {
        if (buf.host[i] != VAL) {
            fprintf(stderr, "buf[%d] = %x\n", i, buf.host[i]);
            success = false;
        }
    }
    delete_image(&buf);
    fprintf(stderr, "test_copy %s\n", success ? "SUCCEEDED" : "FAILED");
}

void test_set_red() {
    fprintf(stderr, "Testing set-to-red kernel...");
    bool success = true;
    const int W = 12, H = 32, C = 3;

    buffer_t buf;
    create_empty_image(&buf, W, H, C, sizeof(uint8_t), Interleaved);
    halide_opengl_dev_malloc(NULL, &buf);


    // Run GPU kernel
    void* args[] = { &buf.dev, 0 };
    size_t arg_sizes[] = { 64, 0 };
    halide_opengl_dev_run(
        NULL,
        "fill_red",
        1, 1, 1,                        // blocks
        W, H, 0,                        // threads
        0,                              // shared_mem_bytes
        arg_sizes, args);

    // Copy back to host memory and check result
    buf.dev_dirty = true;
    halide_opengl_copy_to_host(NULL, &buf);

    uint8_t *ptr = buf.host;
    for (int y=0; y<H; y++) {
        for (int x=0; x<W; x++) {
            if (!(ptr[0] == 0xff &&
                  ptr[1] == 0 &&
                  ptr[2] == 0)) {
                fprintf(stderr, "buf[%d,%d] = (%x,%x,%x)\n",
                        x, y, ptr[0], ptr[1], ptr[2]);
                success = false;
            }
            ptr += 3;
        }
    }
    delete_image(&buf);
    fprintf(stderr, "test_set_red %s\n", success ? "SUCCEEDED" : "FAILED");
}


void test_mockup() {
    // Create GLSL kernels
    halide_opengl_init_kernels(NULL, test_kernel_src, sizeof(test_kernel_src)-1);

    test_copy();
    test_set_red();

    // Create input buffer
    buffer_t inbuf, outbuf;
    create_empty_image(&inbuf, 12, 32, 4, sizeof(uint8_t));
    create_empty_image(&outbuf, 12, 34, 4, sizeof(uint8_t));

    halide_opengl_dev_malloc(NULL, &inbuf);
    halide_opengl_copy_to_dev(NULL, &inbuf);

    halide_opengl_dev_malloc(NULL, &outbuf);

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
