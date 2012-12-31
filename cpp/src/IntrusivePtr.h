#ifndef INTRUSIVE_PTR_H
#define INTRUSIVE_PTR_H

#include <stdlib.h>

namespace Halide { 
namespace Internal {

// A class that represents an intrusive shared ptr to an object
// (i.e. the reference count is managed by the object itself. Any
// class that you want to hold onto via one of these must provide
// implementations of ref_count and destroy
// 
// E.g. if you want to use IntrusivePtr<MyClass>, then you should
// define something like this in MyClass.cpp (assuming MyClass has
// a field: mutable int ref_count):
//
// template<> RefCount &ref_count<MyClass>(const MyClass *c) {return c->ref_count;}
// template<> void destroy<MyClass>(const MyClass *c) {delete c;}

class RefCount {
    int count;
public:
    RefCount() : count(0) {}
    void increment() {count++;}
    void decrement() {count--;}
    bool is_zero() const {return count == 0;}
};

template<typename T> RefCount &ref_count(const T *);
template<typename T> void destroy(const T *);

template<typename T>
struct IntrusivePtr {
private:
        
    void incref(T *ptr) {
        if (ptr) {
            ref_count(ptr).increment();
        }
    };
        
    void decref(T *ptr) {
        if (ptr) {
            ref_count(ptr).decrement();
            if (ref_count(ptr).is_zero()) {
                destroy(ptr);
            }
        }
    }

public:
    T *ptr;

    ~IntrusivePtr() {
        decref(ptr);
    }

    IntrusivePtr() : ptr(NULL) {
    }

    IntrusivePtr(T *p) : ptr(p) {
        incref(ptr);
    }

    IntrusivePtr(const IntrusivePtr<T> &other) : ptr(other.ptr) {
        incref(ptr);
    }

    IntrusivePtr<T> &operator=(const IntrusivePtr<T> &other) {        
        incref(other.ptr);
        decref(ptr);
        ptr = other.ptr;
        return *this;
    }

    /* Handles can be null. This checks that. */
    bool defined() const {
        return ptr;
    }

    /* Check if two handles point to the same ptr. This is
     * equality of reference, not equality of value. */
    bool same_as(const IntrusivePtr &other) const {
        return ptr == other.ptr;
    }
};

}
}

#endif
