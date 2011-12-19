typedef float float32_t;
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
extern "C" {
  #include "curved.h"
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

Image<int16_t> load(const char *filename) {

    FILE *f = fopen(filename, "rb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h;

    fread(&h, sizeof(header_t), 1, f);

    Image<int16_t> im(h.width, h.height);

    for (int y = 0; y < im.height; y++) {
        for (int x = 0; x < im.width; x++) {
            float val;
            fread(&val, sizeof(float), 1, f);
            assert(val >= 0 && val <= 1);
            im(x, y) = int16_t(val * 1023.0f);
        }
    }

    fclose(f);
    return im;
}

void save(Image<uint8_t> &im, const char *filename) {
    FILE *f = fopen(filename, "wb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h {1, im.width, im.height, im.channels, 0};

    
    fwrite(&h, sizeof(header_t), 1, f);

    for (int y = 0; y < im.height; y++) {
        for (int x = 0; x < im.width; x++) {
            for (int c = 0; c < im.channels; c++) {
                float val = (float)(im(x, y, c))/256.0f;
                fwrite(&val, sizeof(float), 1, f);
            }
        }
    }

    fclose(f);
}


int main(int argc, char **argv) {
    Image<int16_t> input = load(argv[1]);
    Image<uint8_t> output(input.width, input.height, 3);

    timeval t1, t2;
    
    gettimeofday(&t1, NULL);
    curved(output.width, output.height, 3,
           input.width, input.height,
           atof(argv[2]), atof(argv[3]),
           &input(0, 0), &output(0, 0));
    gettimeofday(&t2, NULL);

    printf("%3.3f ms\n", (t2.tv_sec - t1.tv_sec)*1000.0f + (t2.tv_usec - t1.tv_usec)/1000.0f);

    save(output, argv[4]);
    return 0;
}
