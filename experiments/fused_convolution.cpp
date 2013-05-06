#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include "fused_convolution_ispc.h"

#define W 640
#define H 6400

class Image {
public:
    float *data;
    size_t width, height;

    Image(size_t w, size_t h) {
        data = new float[w*h];
        width = w;
        height = h;

    }

    inline float &operator()(int x, int y) {
        return data[(y*width + x)];
    }

    inline float operator()(int x, int y) const {
        return data[(y*width + x)];
    }

    ~Image() {
        delete[] data;
    }
};

timeval now() {
    timeval t;
    gettimeofday(&t, NULL);
    return t;
}

float operator-(const timeval &after, const timeval &before) {
    int ds = after.tv_sec - before.tv_sec;
    int du = after.tv_usec - before.tv_usec;
    return (ds * 1000.0f) + (du / 1000.0f);
}

void check(const Image &im) {
    double sum = 0;
    for (int y = 0; y < im.height; y++) {
        for (int x = 0; x < im.width; x++) {
            sum += im(x, y);
        }
    }
    printf("Sum: %f\n", sum); 
}

void compute(int x, int y, Image& tmp, Image& input);

int go() {
    timeval before, after;

    Image input(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            input(x, y) = y/7.0 + x/3.0;
        }
    }

    for (int i = 0; i < 3; i++) {

        printf("\n");

        {
            Image output(W, H);
            before = now();
            for (int y = 16; y < H-16; y++) {
                for (int x = 16; x < W-16; x++) {
                    output(x, y) = input(x, y);
                }
            }
            after = now();

            check(output);

            printf("Copying input to output: %f ms\n", after - before);
        }

        {
            Image output(W, H);
            before = now();
            memcpy(&output(0, 0), &input(0, 0), W*H*sizeof(float));
            after = now();

            check(output);

            printf("Memcpy input to output:  %f ms\n", after - before);
        }
           
        {
            Image tmp(W, H), output(W, H);
        
            before = now();

            for (int y = 4; y < H - 4; y++) {
                for (int x = 4; x < W - 4; x++) {
                    tmp(x, y) = input(x-1, y) + input(x, y) + input(x+1, y);
                }
            }

            for (int y = 4; y < H - 4; y++) {
                for (int x = 4; x < W - 4; x++) {        
                    output(x, y) = tmp(x, y-1) + tmp(x, y) + tmp(x, y+1);
                }
            }        

            after = now();

            check(output);

            printf("Unfused scalar:          %f ms\n", after - before);
        }

        {
            Image tmp(W, H), output(W, H);
        
            before = now();

            ispc::unfused_scalar(W, H, input.data, tmp.data, output.data);

            after = now();

            check(output);

            printf("Unfused scalar ispc:    %f ms\n", after - before);
        }

        {
            Image tmp(W, H), output(W, H);
            before = now();

            for (int y = 4; y < H - 4; y++) {
                for (int x = 4; x < W - 4; x++) {
                    float f = input(x-1, y) + input(x, y) + input(x+1, y);
                    tmp(x, y) = f;
                    output(x, y-1) = tmp(x, (y-2)) + tmp(x, (y-1)) + f;
                }
            }
        
            after = now();

            check(output);

            printf("Fused scalar:            %f ms\n", after - before);
        }

        {
            Image tmp(W, H), output(W, H);
            before = now();
            
            ispc::fused_scalar(W, H, input.data, tmp.data, output.data);
        
            after = now();

            check(output);

            printf("Fused scalar ispc:      %f ms\n", after - before);
        }

        {
            Image tmp(W, 4), output(W, H);
            before = now();

            for (int y = 4; y < H - 4; y++) {
                for (int x = 4; x < W - 4; x++) {
                    float f = input(x-1, y) + input(x, y) + input(x+1, y);
                    tmp(x, y&3) = f;
                    output(x, y-1) = tmp(x, (y-2)&3) + tmp(x, (y-1)&3) + f;
                }
            }
        
            after = now();

            check(output);

            printf("Fused with memory reuse: %f ms\n", after - before);
        }

        {
            Image tmp(W, 4), output(W, H);
            before = now();

            ispc::fused_memory_reuse(W, H, input.data, tmp.data, output.data);
        
            after = now();

            check(output);

            printf("Fused with reuse - ispc: %f ms\n", after - before);
        }

        {
            Image tmp(W, 4), output(W, H);
            before = now();

            for (int y = 4; y < H - 4; y++) {
                for (int x = 4; x < W - 4; x++) {
                    float f = input(x-1, y) + input(x, y) + input(x+1, y);
                    tmp(x, y&3) = f;
                }
                for (int x = 4; x < W - 4; x++) {
                    output(x, y-1) = tmp(x, (y-2)&3) + tmp(x, (y-1)&3) + tmp(x, y&3);
                }
            }
        
            after = now();

            check(output);

            printf("Scanline fusion:         %f ms\n", after - before);
        }

        {
            Image output(W, H);

            Image tmp(W, 4);
            //bool computed[H];
            //memset(computed, 0, sizeof(computed));
//#define COMPUTED(y) (computed[y])
//#define MARK_COMPUTED(y) (computed[y] = true);
            int tmpScanline[4] = {-1, -1, -1, -1};
#define COMPUTED(y) (tmpScanline[y&3] == y)
#define MARK_COMPUTED(y) (tmpScanline[y&3] = y)

            before = now();
            
            for (int y = 4; y < H - 4; y++) {
                for (int x = 4; x < W - 4; x++) {
#if 0
                    if (!computed[y-1]) {
                        int k = (y-1)&3;
                        for (int x = 4; x < W - 4; x++) {
                            tmp(x, k) = input(x-1, y-1) + input(x, y-1) + input(x+1, y-1);
                        }
                        computed[y-1] = true;
                    } 

                    if (!computed[y]) {
                        int k = (y)&3;
                        for (int x = 4; x < W - 4; x++) {
                            tmp(x, k) = input(x-1, y) + input(x, y) + input(x+1, y);
                        }
                        computed[y] = true;
                    } 

                    if (!computed[y+1]) {
                        int k = (y+1)&3;
                        for (int x = 4; x < W - 4; x++) {
                            tmp(x, k) = input(x-1, y+1) + input(x, y+1) + input(x+1, y+1);
                        }
                        computed[y+1] = true;
                    }
#else
                    if (!COMPUTED(y-1)) {
                        compute(x, y-1, tmp, input);
                        MARK_COMPUTED(y-1);
                    }
                    if (!COMPUTED(y  )) {
                        compute(x, y  , tmp, input);
                        MARK_COMPUTED(y);
                    }
                    if (!COMPUTED(y+1)) {
                        compute(x, y+1, tmp, input);
                        MARK_COMPUTED(y+1);
                    }
#endif


                    output(x, y) = tmp(x, (y-1)&3) + tmp(x, y&3) + tmp(x, (y+1)&3);
                }
            }

            after = now();

            check(output);

            printf("Dynamic scheduling:      %f ms\n", after - before);            
        }

    }

    return 0;
}

void compute(int x, int y, Image& tmp, Image& input) {
    int k = (y)&3;
    for (int x = 4; x < W - 4; x++) {
        tmp(x, k) = input(x-1, y) + input(x, y) + input(x+1, y);
    }
}


int main(int argc, char **argv) {
    return go();
}
