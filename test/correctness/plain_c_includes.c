#ifdef __cplusplus
#error "This test must be compiled as plain C, without C++ enabled."
#endif

#include <stdio.h>

// Verify that all HalideRuntime*.h files can be compiled without C++
#include "HalideRuntime.h"
#include "HalideRuntimeCuda.h"
#include "HalideRuntimeHexagonHost.h"
#include "HalideRuntimeMetal.h"
#include "HalideRuntimeOpenCL.h"
#include "HalideRuntimeQurt.h"

int main(int argc, char **argv) {
    printf("Success!\n");
    return 0;
}
