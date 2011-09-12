#include <sys/time.h>
#include <stdio.h>
#include <string.h>

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

int go() {
    timeval before, after;

    for (int i = 0; i < 3; i++) {

        printf("\n");

        {
            Image input(W, H), output(W, H);
            before = now();
            for (int y = 16; y < H-16; y++) {
                for (int x = 16; x < W-16; x++) {
                    output(x, y) = input(x, y);
                }
            }
            after = now();

            printf("Copying input to output: %f ms\n", after - before);
        }

        {
            Image input(W, H), output(W, H);
            before = now();
            memcpy(&output(0, 0), &input(0, 0), W*H*sizeof(float));
            after = now();

            printf("Memcpy input to output:  %f ms\n", after - before);
        }
           
        {
            Image input(W, H), tmp(W, H), output(W, H);
        
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

            printf("Unfused scalar:          %f ms\n", after - before);
        }

        {
            Image input(W, H), tmp(W, H), output(W, H);
            before = now();

            for (int y = 4; y < H - 4; y++) {
                for (int x = 4; x < W - 4; x++) {
                    float f = input(x-1, y) + input(x, y) + input(x+1, y);
                    tmp(x, y) = f;
                    output(x, y-1) = tmp(x, (y-2)) + tmp(x, (y-1)) + f;
                }
            }
        
            after = now();

            printf("Fused scalar:            %f ms\n", after - before);
        }

        {
            Image input(W, H), tmp(W, 4), output(W, H);
            before = now();

            for (int y = 4; y < H - 4; y++) {
                for (int x = 4; x < W - 4; x++) {
                    float f = input(x-1, y) + input(x, y) + input(x+1, y);
                    tmp(x, y&3) = f;
                    output(x, y-1) = tmp(x, (y-2)&3) + tmp(x, (y-1)&3) + f;
                }
            }
        
            after = now();

            printf("Fused with memory reuse: %f ms\n", after - before);
        }

    }

    return 0;
}

int main(int argc, char **argv) {
    return go();
}
