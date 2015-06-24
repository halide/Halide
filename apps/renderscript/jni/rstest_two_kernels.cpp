#include <iostream>
#include <iomanip>
#include "generated_test_two_kernels_rs.h"

buffer_t make_interleaved_image(int width, int height, int channels, uint8_t host[]) {
    buffer_t bt_input = { 0 };
    bt_input.host = (uint8_t*)&host[0];
    bt_input.stride[0] = channels;
    bt_input.extent[0] = width;
    bt_input.stride[1] = channels * width;
    bt_input.extent[1] = height;
    bt_input.stride[2] = 1;
    bt_input.extent[2] = channels;
    bt_input.elem_size = sizeof(uint8_t);
    return bt_input;
}

void print(buffer_t bt) {
    for (int j = 0; j < std::min(bt.extent[1], 10); j++) {
        for (int i = 0; i < std::min(bt.extent[0], 10); i++) {
            std::cout << " [";
            for (int k = 0; k < bt.extent[2]; k++) {
                std::cout << std::fixed << std::setprecision(1);
                if (k > 0) {
                    std::cout << std::setw(4);
                }
                std::cout << +((uint8_t*)bt.host)[i * bt.stride[0] +
                                                j * bt.stride[1] +
                                                k * bt.stride[2]];
            }
            std::cout << "]";
        }

        std::cout << std::endl;
    }
}


extern "C" void halide_set_renderscript_cache_dir(const char *c);
extern "C" int halide_copy_to_host(void *, buffer_t *);

int main(int argc, char** argv) {
    const char *cacheDir = "/data/tmp";
    halide_set_renderscript_cache_dir(cacheDir);

    int width = 10;
    int height = 10;
    int channels = 4;
    uint8_t *input = (uint8_t*)malloc(sizeof(uint8_t) * width * height * channels);
    uint8_t *output = (uint8_t*)malloc(sizeof(uint8_t) * width * height * channels);

    buffer_t input_buft = make_interleaved_image(width, height, channels, input);
    for (int i = 0; i < std::min(input_buft.extent[0], width); i++) {
        for (int j = 0; j < std::min(input_buft.extent[1], height); j++) {
            for (int k = 0; k < input_buft.extent[2]; k++) {
                input[i * input_buft.stride[0] +
                      j * input_buft.stride[1] +
                      k * input_buft.stride[2]] = ((i + j) % 2) * 6;
            }
        }
    }

    std::cout << "Input :\n";
    print(input_buft);

    buffer_t output_buft = make_interleaved_image(width, height, channels, output);

    input_buft.host_dirty = true;
    generated_test_two_kernels_rs(&input_buft, &output_buft);
    halide_copy_to_host(NULL, &output_buft);

    std::cout << "Output :\n";
    print(output_buft);
}
