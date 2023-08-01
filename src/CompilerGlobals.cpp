#include "CompilerGlobals.h"

#include <atomic>

#include "Error.h"

namespace Halide {
namespace Internal {
namespace Globals {

namespace {

// We use 64K of memory to store unique counters for the purpose of
// making names unique. Using less memory increases the likelihood of
// hash collisions. This wouldn't break anything, but makes stmts
// slightly confusing to read because names that are actually unique
// will get suffixes that falsely hint that they are not.
constexpr int num_unique_name_counters = (1 << 14);

struct Storage {
    // A counter to use in random_float() calls.
    std::atomic<int> random_float_counter;

    // A counter to use in random_uint() and random_int() calls.
    std::atomic<int> random_uint_counter;

    // A counter to use in tagging random variables.
    // Note that this will be reset by Internal::reset_random_counters().
    std::atomic<int> random_variable_counter;

    // Counters used for the unique_name() utilities.
    std::atomic<int> unique_name_counters[num_unique_name_counters];

    Storage() {
        random_float_counter = 0;
        random_uint_counter = 0;
        random_variable_counter = 0;
        for (int i = 0; i < num_unique_name_counters; i++) {
            unique_name_counters[i] = 0;
        }
    }

    // std::atomic<> is neither copyable nor movable,
    // so we must manually implement these.
    Storage(const Storage &that) {
        this->copy_from(that);
    }

    Storage &operator=(const Storage &that) {
        // Can't actually happen, but clang-tidy complains if we don't do this
        if (this != &that) {
            this->copy_from(that);
        }
        return *this;
    }

    // Not movable.
    Storage(Storage &&) = delete;
    Storage &operator=(Storage &&) = delete;

    void copy_from(const Storage &that) {
        internal_assert(this != &that);
        (void)this->random_float_counter.exchange(that.random_float_counter);
        (void)this->random_uint_counter.exchange(that.random_uint_counter);
        (void)this->random_variable_counter.exchange(that.random_variable_counter);
        for (int i = 0; i < num_unique_name_counters; i++) {
            (void)this->unique_name_counters[i].exchange(that.unique_name_counters[i]);
        }
    }
};

Storage &storage() {
    static Storage g;
    return g;
}

}  // namespace

int get_random_float_counter() {
    return storage().random_float_counter++;
}

int get_random_uint_counter() {
    return storage().random_uint_counter++;
}

int get_random_variable_counter() {
    return storage().random_variable_counter++;
}

int get_unique_name_counter(size_t hash_value) {
    hash_value &= (num_unique_name_counters - 1);
    return storage().unique_name_counters[hash_value]++;
}

struct SavedGlobalsContents {
    Storage storage;
};

SavedGlobals::SavedGlobals()
    : saved_globals(new SavedGlobalsContents()) {
}

SavedGlobals::~SavedGlobals() {
    delete saved_globals;
}

SavedGlobals::SavedGlobals(SavedGlobals &&that) {
    this->saved_globals = that.saved_globals;
    that.saved_globals = nullptr;
}

SavedGlobals &SavedGlobals::operator=(SavedGlobals &&that) {
    if (this != &that) {
        this->saved_globals = that.saved_globals;
        that.saved_globals = nullptr;
    }
    return *this;
}

SavedGlobals reset() {
    // We can't just reset to the value in our ctor, because statically-initialized
    // things (e.g. Var instances) might have altered us, and resetting the
    // unique_name_counters means that guarantees of unique names will be broken.
    // Instead, initialize a baseline state based on the first time reset() is
    // called, and use *that* for this and all subsequent reset() calls.
    static Storage baseline = storage();

    SavedGlobals previous;
    previous.saved_globals->storage.copy_from(storage());

    storage() = baseline;

    return previous;
}

void restore(SavedGlobals previous) {
    internal_assert(previous.saved_globals != nullptr);
    storage().copy_from(previous.saved_globals->storage);
}

}  // namespace Globals
}  // namespace Internal
}  // namespace Halide
