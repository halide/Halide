#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "alias.h"
#include "alias_with_offset_42.h"
#ifdef HALIDE_ALLOW_LEGACY_AUTOSCHEDULER_API
// nothing
#else
#include "alias_Adams2019.h"
#include "alias_Li2018.h"
#include "alias_Mullapudi2016.h"
#endif

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

#ifdef HALIDE_ALLOW_LEGACY_AUTOSCHEDULER_API
    // nothing
#else
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
#endif

    printf("Success!\n");
    return 0;
}
