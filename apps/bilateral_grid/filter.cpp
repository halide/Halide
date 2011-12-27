#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

extern "C" {
  #include "clarity.h"
}

#include "../Util.h"

int main(int argc, char **argv) {

    Image<uint16_t> input = load<uint16_t>(argv[1]);
    Image<uint16_t> output(input.width(), input.height(), input.channels());

    clarity(10, 0.1f, input, output);
    save(output, argv[2]);

    return 0;
}
