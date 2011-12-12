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

    Var xo, xi;
    //f.split(x, xo, xi, 4);
    //f.vectorize(xi);
    h.root();
    h.split(x, xo, xi, 4);
    h.vectorize(xi);
    g.root();
    g.split(x, xo, xi, 4);
    g.vectorize(xi);

    //f.trace();

    Image<int> out = f.realize(36, 2);

    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 36; x++) {
            if (out(x, y) != x*4 + y) {
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), x*4+y);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
