#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    Var x;

    unsigned int memory = 0;

    printf("memory = %d\n", memory);

    unsigned int *buf1 = &memory;

    void *args = &buf1;

    run(Store(Load(1, 0) + IntImm(17), 1, 0), args);

    printf("memory = %u\n", memory);

    return 0;
}
