#include <FImage.h>
#include <sys/time.h>

using namespace FImage;

double currentTime() {
    timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000.0 + t.tv_usec / 1000.0f;
}

int main(int argc, char **argv) {

    Func f, g, h; Var x, y;
    
    h(x) = x;
    g(x) = h(x-1) + h(x+1);
    f(x, y) = (g(x-1) + g(x+1)) + y;

    h.root();
    g.root();

    if (use_gpu()) {
        Var tidx("threadidx");
        Var bidx("blockidx");
        Var tidy("threadidy");
        Var bidy("blockidy");
        
        f.split(x, bidx, tidx, 16);
        f.parallel(bidx);
        f.parallel(tidx);
        f.split(y, bidy, tidy, 16);
        f.parallel(bidy);
        f.parallel(tidy);
        f.transpose(bidx,tidy);
        
        g.split(x, bidx, tidx, 128);
        g.parallel(bidx);
        g.parallel(tidx);
        
        h.split(x, bidx, tidx, 128);
        h.parallel(bidx);
        h.parallel(tidx);
    }

    //f.trace();

    Image<int> out = f.realize(32, 32);

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            if (out(x, y) != x*4 + y) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), x*4+y);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
