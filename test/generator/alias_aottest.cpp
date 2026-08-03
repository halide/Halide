#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#if defined(TEST_WEBGPU)
#include "HalideRuntimeWebGPU.h"
#endif

#include "alias.h"
#include "alias_Adams2019.h"
#include "alias_Li2018.h"
#include "alias_Mullapudi2016.h"
#include "alias_with_offset_42.h"

using namespace Halide::Runtime;

const int kSize = 32;

int main(int argc, char **argv) {
    Buffer<int32_t, 1> input(kSize), output(kSize);

    input.for_each_element([&](int x) {
        input(x) = x;
    });

    output.fill(0);
    alias(input, output);
    output.copy_to_host();
    input.for_each_element([=](int x) {
        assert(output(x) == input(x));
    });

    output.fill(0);
    alias_with_offset_42(input, output);
    output.copy_to_host();
    input.for_each_element([=](int x) {
        assert(output(x) == input(x) + 42);
    });

    output.fill(0);
    alias_Adams2019(input, output);
    output.copy_to_host();
    input.for_each_element([=](int x) {
        assert(output(x) == input(x) + 2019);
    });

    output.fill(0);
    alias_Li2018(input, output);
    output.copy_to_host();
    input.for_each_element([=](int x) {
        assert(output(x) == input(x) + 2018);
    });

    output.fill(0);
    output.copy_to_host();
    alias_Mullapudi2016(input, output);
    input.for_each_element([=](int x) {
        assert(output(x) == input(x) + 2016);
    });

    printf("Success!\n");

    // WebGPU via Node requires all references to any GPU objects be released,
    // otherwise the process will not exit.
#if defined(TEST_WEBGPU)
    const halide_device_interface_t *interface = halide_webgpu_device_interface();
    input.device_free();
    output.device_free();
    halide_device_release(nullptr, interface);
#endif

    return 0;
}
