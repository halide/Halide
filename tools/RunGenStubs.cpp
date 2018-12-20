#define HALIDE_REGISTER_ARGV_AND_METADATA halide_rungen_register_argv_and_metadata

extern "C" void halide_rungen_register_filter(
    int (*filter_argv_call)(void **),
    const struct halide_filter_metadata_t *filter_metadata,
    void* registerer);

// This is legal C, as long as the macro expands to a
// single quoted (or <>-enclosed) string literal
#include HL_RUNGEN_FILTER_HEADER

/*
    This relies on the filter library being linked in a way that doesn't
    dead-strip "unused" initialization code; this may mean that you need to
    explicitly link with with --whole-archive (or the equivalent) to ensure
    that the registration code isn't omitted. Sadly, there's no portable way
    to do this, so you may need to take care in your make/build/etc files:

    Linux:      -Wl,--whole-archive "/path/to/library" -Wl,-no-whole-archive
    Darwin/OSX: -Wl,-force_load,/path/to/library
    VS2015 R2+: /WHOLEARCHIVE:/path/to/library.lib
    Bazel:      alwayslink=1
*/

namespace {

struct Registerer {
    Registerer() {
        halide_rungen_register_argv_and_metadata();
    }
};

static Registerer registerer;

}  // namespace

