#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

extern "C" int halide_opengl_create_context();


class Image {
public:
    enum Layout {
        Interleaved, Planar
    };

    buffer_t buf;

    Image(int w, int h, int c, int elem_size, Layout layout = Interleaved) {
        memset(&buf, 0, sizeof(buffer_t));
        buf.extent[0] = w;
        buf.extent[1] = h;
        buf.extent[2] = c;
        buf.elem_size = elem_size;

        if (layout == Interleaved) {
            buf.stride[0] = buf.extent[2];
            buf.stride[1] = buf.extent[0] * buf.stride[0];
            buf.stride[2] = 1;
        } else {
            buf.stride[0] = 1;
            buf.stride[1] = buf.extent[0] * buf.stride[0];
            buf.stride[2] = buf.extent[1] * buf.stride[1];
        }
        size_t size = w * h * c * elem_size;
        buf.host = (uint8_t*)malloc(size);
        memset(buf.host, 0, size);
        buf.host_dirty = true;
    }
    ~Image() {
        halide_opengl_dev_free(NULL, &buf);
        free(buf.host);
    }
};

#include "blur.h"
#include "ycc.h"

void test_compiled_filter() {
    const int W = 12, H = 32, C = 3;
    Image input(W, H, C, sizeof(uint8_t), Image::Planar);
    Image output(W, H, C, sizeof(uint8_t), Image::Planar);

    ycc_filter(&input.buf, &output.buf);
    fprintf(stderr, "Ycc complete\n");
    blur_filter(&input.buf, &output.buf);
}

void test_copy() {
    fprintf(stderr, "test_copy: ");
    fflush(stderr);

    bool success = true;
    const int W = 12, H = 32, C = 3;
    const uint8_t VAL = 0x1c;

    Image img(W, H, C, sizeof(uint8_t), Image::Planar);

    size_t size = W * H * C * sizeof(uint8_t);

    memset(img.buf.host, VAL, size);

    halide_opengl_dev_malloc(NULL, &img.buf);
    halide_opengl_copy_to_dev(NULL, &img.buf);

    memset(img.buf.host, 0, size);

    img.buf.dev_dirty = true;

    halide_opengl_copy_to_host(NULL, &img.buf);

    for (int i = 0; i < size; i++) {
        if (img.buf.host[i] != VAL) {
            fprintf(stderr, "buf[%d] = %x\n", i, img.buf.host[i]);
            success = false;
        }
    }
    fprintf(stderr, "%s\n", success ? "SUCCEEDED" : "FAILED");
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

void test_set_red() {
    fprintf(stderr, "Testing set-to-red kernel...");
    bool success = true;
    const int W = 12, H = 32, C = 3;

    Image img(W, H, C, sizeof(uint8_t), Image::Interleaved);
    halide_opengl_dev_malloc(NULL, &img.buf);


    // Run GPU kernel
    void* args[] = { &img.buf.dev, 0 };
    size_t arg_sizes[] = { 64, 0 };
    halide_opengl_dev_run(
        NULL,
        "fill_red",
        1, 1, 1,                        // blocks
        W, H, 0,                        // threads
        0,                              // shared_mem_bytes
        arg_sizes, args);

    // Copy back to host memory and check result
    img.buf.dev_dirty = true;
    halide_opengl_copy_to_host(NULL, &img.buf);

    uint8_t *ptr = img.buf.host;
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
    fprintf(stderr, "test_set_red %s\n", success ? "SUCCEEDED" : "FAILED");
}


void test_mockup() {
    // Create GLSL kernels
    halide_opengl_init_kernels(NULL, test_kernel_src, sizeof(test_kernel_src)-1);

    test_set_red();

    // Create input buffer
    Image input(12, 32, 4, sizeof(uint8_t));
    Image output(12, 34, 4, sizeof(uint8_t));

    halide_opengl_dev_malloc(NULL, &input.buf);
    halide_opengl_copy_to_dev(NULL, &input.buf);

    halide_opengl_dev_malloc(NULL, &output.buf);

    // Run one of the kernels
    int arg_xpos = 11;
    float arg_parameter = 3.14159f;
    void* args[] = {
        &input.buf.dev, &arg_xpos, &arg_parameter, &output.buf.dev, 0
    };
    size_t arg_sizes[] = {
        64, sizeof(arg_xpos)*4, sizeof(arg_parameter)*4, 64, 0
    };
    halide_opengl_dev_run(
        NULL,
        "test_kernel",
        1, 1, 1,                        // blocks
        output.buf.extent[0], output.buf.extent[1], output.buf.extent[2], // threads
        0,                              // shared_mem_bytes
        arg_sizes, args);

    // Copy back to host memory and check result
    halide_opengl_copy_to_host(NULL, &output.buf);
    // TODO: check result

    // Cleanup
    halide_opengl_dev_free(NULL, &input.buf);
    halide_opengl_dev_free(NULL, &output.buf);
//    halide_opengl_release(NULL);
}

int main(int argc, char* argv[]) {
    if (halide_opengl_create_context() != 0) {
	fprintf(stderr, "Could not create OpenGL context\n");
        exit(1);
    }
    test_copy();
    test_compiled_filter();
    test_mockup();
}
