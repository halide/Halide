#include <FImage.h>
#include <sys/time.h>

using namespace FImage;

int main(int argc, char **argv) {

    const int K = 8;

    Func f[K]; Var x, y;

    if (argc > 1) srand(atoi(argv[1]));
    else srand(0);
    
    f[0](x, y) = x+y;
    f[1](x, y) = x*y;
    for (int i = 2; i < K; i++) {
        int j1 = rand() % i;
        int j2 = rand() % i;
        int j3 = rand() % i; 
        f[i](x, y) = f[j1](x-1, y-1) + f[j2](x+1, clamp(f[j3](x+1, y-1), 0, 7));

        if (i < K-1) {
            switch (rand() % 3) {
            case 0:
                f[i].root();
                f[i].vectorize(x, 4);
                break;
            case 1:
                f[i].chunk(y);
                f[i].vectorize(x, 4);
                break;
            default:
                break;
            }
        }
    }

    //f[K-1].trace();

    Image<int> out = f[K-1].realize(32, 32);

    printf("Success!\n");
    return 0;
}

/* 
Images as Functions

We argue for images as (mostly) pure functions because it separates the schedule from the algorithm.

*/
