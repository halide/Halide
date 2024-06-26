#include "Halide.h"

using namespace Halide;

// Print the host target to stdout.
// Any extra arguments are assumed to be features that should be stripped from
// the target (as a convenience for use in Makefiles, where string manipulation
// can be painful).
int main(int argc, char **argv) {
    Target t = get_host_target();
    for (int i = 1; i < argc; ++i) {
        auto f = Target::feature_from_name(argv[i]);
        if (f == Target::FeatureEnd) {
            fprintf(stderr, "Unknown feature: %s\n", argv[i]);
            exit(1);
        }
        t = t.without_feature(f);
    }
    printf("%s", t.to_string().c_str());
    return 0;
}
