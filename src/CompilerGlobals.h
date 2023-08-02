#ifndef HALIDE_COMPILER_GLOBALS_H
#define HALIDE_COMPILER_GLOBALS_H

/** \file
 * Contains an interface that manages all of the Compiler's global mutable state.
 * The actual storage is deliberately hidden. The entire API is intended to
 * be thread-safe.
 */

#include <cstddef>
#include <utility>

namespace Halide {
namespace Internal {
namespace Globals {

// Return a new, unique counter to use in random_float() calls.
int get_random_float_counter();

// Return a new, unique counter to use in random_uint() and random_int() calls.
int get_random_uint_counter();

// Return a new, unique counter for tagging Variables associated with
// random_float/int/uint() calls.
int get_random_variable_counter();

// Return a new, usually-unique counter for a name with the given hash value.
// It is *not* guaranteed to be unique in all cases, but usually will be.
int get_unique_name_counter(size_t hash_value);

// SavedGlobals is a magic cookie used for reset() and restore().
struct SavedGlobalsContents;

struct SavedGlobals {
    SavedGlobalsContents *saved_globals = nullptr;

    SavedGlobals();
    ~SavedGlobals();

    // movable, not copyable
    SavedGlobals(const SavedGlobals &) = delete;
    SavedGlobals &operator=(const SavedGlobals &) = delete;
    SavedGlobals(SavedGlobals &&) noexcept;
    SavedGlobals &operator=(SavedGlobals &&) noexcept;
};

// Reset the current state of the Globals to the default and return an opaque
// instance that can be used to retore the previous state.
SavedGlobals reset();

// Restore the Globals state represented by SavedGlobals.
void restore(SavedGlobals previous);

// Utility class for save-reset-restore the Globals.
class ScopedSavedGlobals {
    SavedGlobals saved_globals;

public:
    ScopedSavedGlobals()
        : saved_globals(reset()) {
    }

    ~ScopedSavedGlobals() {
        restore(std::move(saved_globals));
    }
};

}  // namespace Globals
}  // namespace Internal
}  // namespace Halide

#endif
