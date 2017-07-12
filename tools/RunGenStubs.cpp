#define HALIDE_GET_STANDARD_ARGV_FUNCTION halide_rungen_redirect_argv_getter
#define HALIDE_GET_STANDARD_METADATA_FUNCTION halide_rungen_redirect_metadata_getter

// This is legal C, as long as the macro expands to a single quoted (or <>-enclosed) string literal
#include HL_RUNGEN_FILTER_HEADER

extern "C" int halide_rungen_redirect_argv(void **args) {
    return halide_rungen_redirect_argv_getter()(args);
}

extern "C" const struct halide_filter_metadata_t *halide_rungen_redirect_metadata() {
    return halide_rungen_redirect_metadata_getter()();
}
