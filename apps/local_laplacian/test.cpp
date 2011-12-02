#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

typedef float float32_t;

extern "C" {
#include "f5.h"
}

struct Image {
    float *data;
    int width, height, channels;
};

Image load(const char *filename) {

    FILE *f = fopen(filename, "rb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h;

    fread(&h, sizeof(header_t), 1, f);

    Image im = {new float[h.width * h.height * h.channels], h.width, h.height, h.channels};
    
    for (int y = 0; y < im.height; y++) {
        for (int x = 0; x < im.width; x++) {
            for (int c = 0; c < im.channels; c++) {
                fread(im.data + (x + im.width*(y + im.height*c)), sizeof(float), 1, f);
            }
        }
    }

    fclose(f);
    return im;
}

void save(Image im, const char *filename) {
    FILE *f = fopen(filename, "wb");

    // get the dimensions
    struct header_t {
        int frames, width, height, channels, typeCode;
    } h = {1, im.width, im.height, im.channels, 0};

    
    fwrite(&h, sizeof(header_t), 1, f);

    for (int y = 0; y < im.height; y++) {
        for (int x = 0; x < im.width; x++) {
            for (int c = 0; c < im.channels; c++) {
                fwrite(im.data + (x + im.width*(y + im.height*c)), sizeof(float), 1, f);
            }
        }
    }

    fclose(f);
}

int main(int argc, char **argv) {
    Image im = load(argv[1]);
    Image out = {new float[im.width * im.height * im.channels], im.width, im.height, im.channels};

    timeval t1, t2;
    
    gettimeofday(&t1, NULL);

    f5(im.width, im.height, im.channels, im.width, im.height,
       atoi(argv[3]), atof(argv[4]), atof(argv[5]), im.data, out.data);

    gettimeofday(&t2, NULL);

    printf("%3.3f ms\n", (t2.tv_sec - t1.tv_sec)*1000.0f + (t2.tv_usec - t1.tv_usec)/1000.0f);

    save(out, argv[2]);
    return 0;
}
