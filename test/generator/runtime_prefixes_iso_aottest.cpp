#include "HalideBuffer.h"
#include <cstdio>
#include <cstdlib>
#include <string>

// One header per prefixes variant. All variants come from the same generator,
// each linked against a runtime with a distinct symbol prefix:
//   - "none": the stock halide_ runtime.
//   - "a":    export/import prefix "runtime_a_", internal prefix "runtime_ai_".
//   - "b":    export/import prefix "runtime_b_", internal prefix "runtime_bi_".
// Each variant is built with both the LLVM backend and the C backend (the "_c"
// functions), so both backends' namespacing is exercised.
#include "rniso_a.h"
#include "rniso_a_c.h"
#include "rniso_b.h"
#include "rniso_b_c.h"
#include "rniso_none.h"
#include "rniso_none_c.h"

using Halide::Runtime::Buffer;

// Per-runtime allocation counters -- the state that must stay independent.
static int count_none = 0, count_a = 0, count_b = 0;

// Aligned counting allocator (mirrors test/correctness/custom_allocator.cpp).
template<int *Counter>
void *counting_malloc(void *, size_t x) {
    (*Counter)++;
    void *orig = malloc(x + 32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}
void counting_free(void *, void *ptr) {
    free(((void **)ptr)[-1]);
}

// The renamed public runtime API for the "a"/"b" runtimes. The matching types
// (halide_malloc_t, halide_error_handler_t, ...) and the "none" runtime's
// halide_* entry points come from HalideRuntime.h via the generated headers.
extern "C" {
// Custom allocator (a function-pointer piece of state).
halide_malloc_t runtime_a_set_custom_malloc(halide_malloc_t);
halide_free_t runtime_a_set_custom_free(halide_free_t);
halide_malloc_t runtime_b_set_custom_malloc(halide_malloc_t);
halide_free_t runtime_b_set_custom_free(halide_free_t);

// Thread count (an integer piece of state, stored in the thread pool).
int runtime_a_set_num_threads(int);
int runtime_a_get_num_threads();
int runtime_b_set_num_threads(int);
int runtime_b_get_num_threads();

// Error handler (another function-pointer piece of state), plus the entry
// point that dispatches through it.
halide_error_handler_t runtime_a_set_error_handler(halide_error_handler_t);
halide_error_handler_t runtime_b_set_error_handler(halide_error_handler_t);
void runtime_a_error(void *, const char *);
void runtime_b_error(void *, const char *);
}

// Per-runtime record of the last error message routed through its handler.
static const char *last_error_none = nullptr;
static const char *last_error_a = nullptr;
static const char *last_error_b = nullptr;
void error_none(void *, const char *msg) {
    last_error_none = msg;
}
void error_a(void *, const char *msg) {
    last_error_a = msg;
}
void error_b(void *, const char *msg) {
    last_error_b = msg;
}

namespace {

bool run_and_check(int (*fn)(halide_buffer_t *), const char *name) {
    Buffer<int32_t> out(16);
    int r = fn(out);
    if (r != 0) {
        printf("%s returned %d\n", name, r);
        return false;
    }
    for (int x = 0; x < 16; x++) {
        // output(x) = 2x + 2(x+1) = 4x + 2
        const int expected = 4 * x + 2;
        if (out(x) != expected) {
            printf("%s: out(%d) = %d, expected %d\n", name, x, out(x), expected);
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    // Install a distinct counting allocator into each runtime's state.
    halide_set_custom_malloc(counting_malloc<&count_none>);
    halide_set_custom_free(counting_free);
    runtime_a_set_custom_malloc(counting_malloc<&count_a>);
    runtime_a_set_custom_free(counting_free);
    runtime_b_set_custom_malloc(counting_malloc<&count_b>);
    runtime_b_set_custom_free(counting_free);

    struct Variant {
        const char *name;
        int (*fn)(halide_buffer_t *);
        int *counter;
    };
    const Variant variants[] = {
        // LLVM backend.
        {"none", pipe_none, &count_none},
        {"a", pipe_a, &count_a},
        {"b", pipe_b, &count_b},
        // C backend (same runtimes, so the counters are shared with the LLVM
        // variants of the same namespace).
        {"none_c", pipe_none_c, &count_none},
        {"a_c", pipe_a_c, &count_a},
        {"b_c", pipe_b_c, &count_b},
    };

    // Run each variant in turn. After each run, exactly one runtime's allocation
    // counter must have advanced -- proving each pipeline uses its own runtime
    // and that the runtimes' state is independent.
    for (const auto &v : variants) {
        const int own_before = *v.counter;
        const int before_none = count_none, before_a = count_a, before_b = count_b;

        if (!run_and_check(v.fn, v.name)) {
            return 1;
        }

        // This variant must have used its own runtime's allocator...
        if (*v.counter <= own_before) {
            printf("FAIL: variant '%s' did not use its own runtime allocator\n", v.name);
            return 1;
        }
        // ...and no other runtime's state may have changed.
        const bool none_ok = (v.counter == &count_none) || (count_none == before_none);
        const bool a_ok = (v.counter == &count_a) || (count_a == before_a);
        const bool b_ok = (v.counter == &count_b) || (count_b == before_b);
        if (!none_ok || !a_ok || !b_ok) {
            printf("FAIL: variant '%s' disturbed another runtime's state "
                   "(none=%d a=%d b=%d)\n",
                   v.name, count_none, count_a, count_b);
            return 1;
        }
        printf("variant '%s' ok: none=%d a=%d b=%d\n", v.name, count_none, count_a, count_b);
    }

    // ------------------------------------------------------------------
    // Thread count: an integer piece of runtime state. Set a distinct value in
    // each runtime and read it back -- each getter must see only its own value.
    // ------------------------------------------------------------------
    halide_set_num_threads(2);
    runtime_a_set_num_threads(3);
    runtime_b_set_num_threads(4);
    if (halide_get_num_threads() != 2 ||
        runtime_a_get_num_threads() != 3 ||
        runtime_b_get_num_threads() != 4) {
        printf("FAIL: thread counts not independent: none=%d a=%d b=%d\n",
               halide_get_num_threads(), runtime_a_get_num_threads(), runtime_b_get_num_threads());
        return 1;
    }
    printf("thread counts ok: none=%d a=%d b=%d\n",
           halide_get_num_threads(), runtime_a_get_num_threads(), runtime_b_get_num_threads());

    // ------------------------------------------------------------------
    // Error handler: a function-pointer piece of runtime state. Install a
    // distinct handler in each runtime, dispatch an error through each runtime's
    // halide_error, and confirm each error reached only its own handler.
    // ------------------------------------------------------------------
    halide_set_error_handler(error_none);
    runtime_a_set_error_handler(error_a);
    runtime_b_set_error_handler(error_b);

    halide_error(nullptr, "err_none");
    runtime_a_error(nullptr, "err_a");
    runtime_b_error(nullptr, "err_b");

    const bool errors_ok =
        last_error_none && std::string(last_error_none) == "err_none" &&
        last_error_a && std::string(last_error_a) == "err_a" &&
        last_error_b && std::string(last_error_b) == "err_b";
    if (!errors_ok) {
        printf("FAIL: error handlers not independent: none=%s a=%s b=%s\n",
               last_error_none ? last_error_none : "(null)",
               last_error_a ? last_error_a : "(null)",
               last_error_b ? last_error_b : "(null)");
        return 1;
    }
    printf("error handlers ok: none=%s a=%s b=%s\n", last_error_none, last_error_a, last_error_b);

    printf("Success!\n");
    return 0;
}
