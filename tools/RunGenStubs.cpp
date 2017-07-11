#ifndef HL_RUNGEN_FILTER
    #error "You must define HL_RUNGEN_FILTER"
#endif

#define HL_RUNGEN_NAME__(prefix, suffix) prefix##suffix
#define HL_RUNGEN_NAME_(prefix, suffix) HL_RUNGEN_NAME__(prefix, suffix)
#define HL_RUNGEN_NAME(suffix) HL_RUNGEN_NAME_(HL_RUNGEN_FILTER, suffix)

struct halide_filter_metadata_t;

extern "C" int HL_RUNGEN_NAME(_argv)(void **args);
extern "C" const struct halide_filter_metadata_t *HL_RUNGEN_NAME(_metadata)();

extern "C" int halide_rungen_redirect_argv(void **args) {
    return HL_RUNGEN_NAME(_argv)(args);
}

extern "C" const struct halide_filter_metadata_t *halide_rungen_redirect_metadata() {
    return HL_RUNGEN_NAME(_metadata)();
}
