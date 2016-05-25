#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target host = get_host_target();
    printf("Host is: %s\n", host.to_string().c_str());

    uint64_t host_features = 0;
    for (int i = 0; i < (int)(Target::FeatureEnd); i++) {
        if (host.has_feature((Target::Feature)i)) {
            host_features |= ((uint64_t)1) << i;
        }
    }

    printf("host_features are: %llx\n", (unsigned long long) host_features);

    // First, test that the host features are usable. If not, something is wrong.
    if (!halide_can_use_target_features(host_features)) {
      printf("Failure!\n");
      return -1;
    }

    // Now start subtracting features; we should still be usable
    for (int i = 0; i < (int)(Target::FeatureEnd); i++) {
        if (host.has_feature((Target::Feature)i)) {
            host_features &= ~((uint64_t)1) << i;
            if (!halide_can_use_target_features(host_features)) {
              printf("Failure!\n");
              return -1;
            }
        }
    }

    // Finally, check the empty set of features; this should always pass.
    if (!halide_can_use_target_features(0)) {
      printf("Failure!\n");
      return -1;
    }

    printf("Success!\n");
    return 0;
}
