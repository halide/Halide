#ifndef HALIDE_INTRUSIVE_PTR_H
#define HALIDE_INTRUSIVE_PTR_H

/** \file
 *
 * Support classes for reference-counting via intrusive shared
 * pointers.
 */

#include <stdlib.h>
#include <atomic>

#include "Util.h"

namespace Halide {
namespace Internal {

/** A class representing a reference count to be used with IntrusivePtr */
class RefCount {
    std::atomic<int> count;
public:
    RefCount() : count(0) {}
    int increment() {return ++count;} // Increment and return new value
    int decrement() {return --count;} // Decrement and return new value
    bool is_zero() const {return count == 0;}
};

/**
 * Because in this header we don't yet know how client classes store
 * their RefCount (and we don't want to depend on the declarations of
 * the client classes), any class that you want to hold onto via one
 * of these must provide implementations of ref_count and destroy,
 * which we forward-declare here.
 *
 * E.g. if you want to use IntrusivePtr<MyClass>, then you should
 * define something like this in MyClass.cpp (assuming MyClass has
 * a field: mutable RefCount ref_count):
 *
 * template<> RefCount &ref_count<MyClass>(const MyClass *c) {return c->ref_count;}
 * template<> void destroy<MyClass>(const MyClass *c) {delete c;}
 */
// @{
template<typename T> RefCount &ref_count(const T *t);
template<typename T> void destroy(const T *t);
// @}

/** Intrusive shared pointers have a reference count (a
 * RefCount object) stored in the class itself. This is perhaps more
 * efficient than storing it externally, but more importantly, it
 * means it's possible to recover a reference-counted handle from the
 * raw pointer, and it's impossible to have two different reference
 * counts attached to the same raw object. Seeing as we pass around
 * raw pointers to concrete IRNodes and Expr's interchangeably, this
 * is a useful property.
 */
template<typename T>
struct IntrusivePtr {
private:

    void incref(T *p) {
        if (p) {
            ref_count(p).increment();
        }
    };

    void decref(T *p) {
        if (p) {
            // Note that if the refcount is already zero, then we're
            // in a recursive destructor due to a self-reference (a
            // cycle), where the ref_count has been adjusted to remove
            // the counts due to the cycle. The next line then makes
            // the ref_count negative, which prevents actually
            // entering the destructor recursively.
            if (ref_count(p).decrement() == 0) {
                destroy(p);
            }
        }
    }

protected:
    T *ptr;

public:
    /** Access the raw pointer in a variety of ways.
     * Note that a "const IntrusivePtr<T>" is not the same thing as an
     * IntrusivePtr<const T>. So the methods that return the ptr are
     * const, despite not adding an extra const to T. */
    // @{
    T *get() const {
        return ptr;
    }

    T &operator*() const {
        return *ptr;
    }

    T *operator->() const {
        return ptr;
    }
    // @}

    ~IntrusivePtr() {
        decref(ptr);
    }

    HALIDE_ALWAYS_INLINE
    IntrusivePtr() : ptr(nullptr) {
    }

    HALIDE_ALWAYS_INLINE
    IntrusivePtr(T *p) : ptr(p) {
        incref(ptr);
    }

    HALIDE_ALWAYS_INLINE
    IntrusivePtr(const IntrusivePtr<T> &other) : ptr(other.ptr) {
        incref(ptr);
    }

    HALIDE_ALWAYS_INLINE
    IntrusivePtr(IntrusivePtr<T> &&other) noexcept : ptr(other.ptr) {
        other.ptr = nullptr;
    }

    IntrusivePtr<T> &operator=(const IntrusivePtr<T> &other) {
        if (other.ptr == ptr) return *this;
        // Other can be inside of something owned by this, so we
        // should be careful to incref other before we decref
        // ourselves.
        T *temp = other.ptr;
        incref(temp);
        decref(ptr);
        ptr = temp;
        return *this;
    }

    IntrusivePtr<T> &operator=(IntrusivePtr<T> &&other) noexcept {
        std::swap(ptr, other.ptr);
        return *this;
    }

    /* Handles can be null. This checks that. */
    HALIDE_ALWAYS_INLINE
    bool defined() const {
        return ptr != nullptr;
    }

    /* Check if two handles point to the same ptr. This is
     * equality of reference, not equality of value. */
    HALIDE_ALWAYS_INLINE
    bool same_as(const IntrusivePtr &other) const {
        return ptr == other.ptr;
    }

    HALIDE_ALWAYS_INLINE
    bool operator<(const IntrusivePtr<T> &other) const {
        return ptr < other.ptr;
    }
};

}  // namespace Internal
}  // namespace Halide

#endif
