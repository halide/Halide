#ifndef HALIDE_FUNCTION_PTR_H
#define HALIDE_FUNCTION_PTR_H

#include "IntrusivePtr.h"

namespace Halide {
namespace Internal {

/** Functions are allocated in groups for memory management. Each
 * group has a ref count associated with it. All within-group
 * references must be weak. If there are any references from outside
 * the group, at least one must be strong.  Within-group references
 * may form cycles, but there may not be reference cycles that span
 * multiple groups. These rules are not enforced automatically. */
struct FunctionGroup;

/** The opaque struct describing a Halide function. Wrap it in a
 * Function object to access it. */
struct FunctionContents;

/** A possibly-weak pointer to a Halide function. Take care to follow
 * the rules mentioned above. Preserves weakness/strength on copy.
 *
 * Note that Function objects are always strong pointers to Halide
 * functions.
 */
struct FunctionPtr {
    /** A strong and weak pointer to the group. Only one of these
     * should be non-zero. */
    // @{
    IntrusivePtr<FunctionGroup> strong;
    FunctionGroup *weak = nullptr;
    // @}

    /** The index of the function within the group. */
    int idx = 0;

    /** Get a pointer to the group this Function belongs to. */
    FunctionGroup *group() const {
        return weak ? weak : strong.get();
    }

    /** Get the opaque FunctionContents object this pointer refers
     * to. Wrap it in a Function to do anything interesting with it. */
    // @{
    FunctionContents *get() const;

    FunctionContents &operator*() const {
        return *get();
    }

    FunctionContents *operator->() const {
        return get();
    }
    // @}

    /** Convert from a strong reference to a weak reference. Does
     * nothing if the pointer is undefined, or if the reference is
     * already weak. */
    void weaken() {
        weak = group();
        strong = nullptr;
    }

    /** Convert from a weak reference to a strong reference. Does
     * nothing if the pointer is undefined, or if the reference is
     * already strong. */
    void strengthen() {
        strong = group();
        weak = nullptr;
    }

    /** Check if the reference is defined. */
    bool defined() const {
        return weak || strong.defined();
    }

    /** Check if two FunctionPtrs refer to the same Function. */
    bool same_as(const FunctionPtr &other) const {
        return idx == other.idx && group() == other.group();
    }

    /** Pointer comparison, for using FunctionPtrs as keys in maps and
     * sets. */
    bool operator<(const FunctionPtr &other) const {
        return get() < other.get();
    }
};

}  // namespace Internal
}  // namespace Halide

#endif
