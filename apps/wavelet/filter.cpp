#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

extern "C" {
  #include "haar_x.h"
  #include "inverse_haar_x.h"
  #include "daubechies_x.h"
  #include "inverse_daubechies_x.h"
}

#include <static_image.h>
#include <image_io.h>

float clamp(float x, float min, float max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

void save_transformed(Image<float> t, const char *filename) {
    Image<float> rearranged(t.width()*2, t.height(), 1);
    for (int y = 0; y < t.height(); y++) {
        for (int x = 0; x < t.width(); x++) {
            rearranged(x, y, 0) = clamp(t(x, y, 0), 0.0f, 1.0f);
            rearranged(x + t.width(), y, 0) = clamp(t(x, y, 1)*4 + 0.5, 0.0f, 1.0f);
        }
    }
    save(rearranged, filename);
}

int main(int argc, char **argv) {

    Image<float> input = load<float>(argv[1]);
    Image<float> transformed(input.width()/2, input.height(), 2);
    Image<float> inverse_transformed(input.width(), input.height(), 1);

    printf("haar_x\n");
    int result = haar_x(input, transformed);
    if (result != 0) {
        printf("filter failed: %d\n", result);
        return -1;
    }
    printf("saving result...\n");
    save_transformed(transformed, "haar_x.png");

    printf("inverse_haar_x\n");
    result = inverse_haar_x(transformed, inverse_transformed);
    if (result != 0) {
        printf("filter failed: %d\n", result);
        return -1;
    }
    printf("saving result...\n");
    save(inverse_transformed, "inverse_haar_x.png");

    printf("daubechies_x\n");
    result = daubechies_x(input, transformed);
    if (result != 0) {
        printf("filter failed: %d\n", result);
        return -1;
    }
    printf("saving result...\n");
    save_transformed(transformed, "daubechies_x.png");

    printf("inverse_daubechies_x\n");
    result = inverse_daubechies_x(transformed, inverse_transformed);
    if (result != 0) {
        printf("filter failed: %d\n", result);
        return -1;
    }
    printf("saving result...\n");
    save(inverse_transformed, "inverse_daubechies_x.png");

    return 0;
}
