typedef float float32_t;
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
extern "C" {
  #include "bilateral_grid.h"
}

template<typename T>
struct Image {
    Image(int w, int h, int c = 1) {
        width = w;
        height = h;
        channels = c;
        data = new T[w*h*c+16];

        // Force the base address to be 16-byte aligned
        base = 0;
        while ((size_t)(data + base) % 16 != 0) base++;
    }

    T &operator()(int x, int y, int c = 0) {
        return data[(c*height + y)*width + x + base];
    }

    int width, height, channels, base;
    T *data;
};

Image<uint16_t> load(const char *filename) {

    FILE *f = fopen(filename, "rb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h;

    fread(&h, sizeof(header_t), 1, f);

    Image<uint16_t> im(h.width, h.height);

    for (int y = 0; y < im.height; y++) {
        for (int x = 0; x < im.width; x++) {
            float val;
            fread(&val, sizeof(float), 1, f);
            assert(val >= 0 && val <= 1);
            im(x, y) = uint16_t(val * 65535.0f);
        }
    }

    fclose(f);
    return im;
}

void save(Image<uint16_t> &im, const char *filename) {
    FILE *f = fopen(filename, "wb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h {1, im.width, im.height, 1, 0};

    
    fwrite(&h, sizeof(header_t), 1, f);

    for (int y = 0; y < im.height; y++) {
        for (int x = 0; x < im.width; x++) {
            float val = (float)(im(x, y))/(65535.0f);
            fwrite(&val, sizeof(float), 1, f);
        }
    }

    fclose(f);
}

int main(int argc, char **argv) {

    Image<uint16_t> input = load(argv[1]);
    Image<uint16_t> output(input.width, input.height);
    bilateral_grid(input.width, input.height, 10, input.width, input.height, 10000.0f, input.data, output.data);
    //bilateral_grid(input.width, input.height, input.width, input.height, input.data, output.data);
    save(output, argv[2]);

    return 0;
}
