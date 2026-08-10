# Verify the shipped add_halide_library rules routed HL_CACHE_DIR to the
# generator process: a generator that saw HL_CACHE_DIR creates an "entries"
# directory under the cache. We check for that directory rather than inspecting
# its hash-named contents -- the hit/miss semantics themselves are covered by
# test/correctness/generator_cache.cpp.
if (NOT IS_DIRECTORY "${CACHE_DIR}/entries")
    message(
        FATAL_ERROR "Cache at '${CACHE_DIR}' was not populated (no 'entries' directory); "
        "HL_CACHE_DIR did not reach the generator process."
    )
endif ()

message(STATUS "Generator cache populated at '${CACHE_DIR}'.")
