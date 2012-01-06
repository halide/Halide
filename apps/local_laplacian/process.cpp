#include <stdio.h>
extern "C" {
    #include "local_laplacian.h"
}
#include "../Util.h"
#include "../png.h"
#include <sys/time.h>

using namespace FImage;

int main(int argc, char **argv) {
    Image<uint16_t> input = load<uint16_t>(argv[1]);
    int levels = atoi(argv[2]);
    float alpha = atof(argv[3]), beta = atof(argv[4]);    
    Image<uint16_t> output(input.width(), input.height(), 3);

    timeval t1, t2;
    gettimeofday(&t1, NULL);
    local_laplacian(levels, beta, alpha/(levels-1), input, output);
    gettimeofday(&t2, NULL);

    int t = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_usec - t1.tv_usec);
    printf("%u\n", t);

    save(output, argv[5]);

    return 0;
}
