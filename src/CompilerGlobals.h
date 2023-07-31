#ifndef HALIDE_COMPILER_GLOBALS_H
#define HALIDE_COMPILER_GLOBALS_H

/** \file
 * Contains a struct that defines all of the Compiler's global state.
 */

#include <atomic>

#include "Util.h"  // for num_unique_name_counters

namespace Halide {
namespace Internal {

/** This struct is designed to contain all of the *mutable* global data
 * used by the Halide compiler. (Global data that is declared const must
 * not go here.) */
struct Globals {
    // A counter to use in random_float() calls.
    std::atomic<int> random_float_counter;

    // A counter to use in random_uint() and random_int() calls.
    std::atomic<int> random_uint_counter;

    // A counter to use in tagging random variables.
    // Note that this will be reset by Internal::reset_random_counters().
    std::atomic<int> random_variable_counter;

    // Counters used for the unique_name() utilities.
    std::atomic<int> unique_name_counters[num_unique_name_counters];

    // Reset all the globals to their default values.
    void reset();

    Globals();
    Globals(const Globals &copy) = delete;
    Globals &operator=(const Globals &) = delete;
    Globals(Globals &&) = delete;
    Globals &operator=(Globals &&) = delete;
};

Globals &globals();

}  // namespace Internal
}  // namespace Halide

#endif
