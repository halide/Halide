#ifndef HALIDE_INTRUSIVE_PTR_H
#define HALIDE_INTRUSIVE_PTR_H

/** \file
 * 
 * Support classes for reference-counting via intrusive shared
 * pointers.
 */ 

#include <stdlib.h>
#include <iostream>
namespace Halide { 
namespace Internal {

/** A class representing a reference count to be used with IntrusivePtr */
class RefCount {
    int count;
public:
    RefCount() : count(0) {}
    void increment() {count++;}
    void decrement() {count--;}
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
 * a field: mutable int ref_count):
 *
 * template<> RefCount &ref_count<MyClass>(const MyClass *c) {return c->ref_count;}
 * template<> void destroy<MyClass>(const MyClass *c) {delete c;}
 */
// @{
template<typename T> RefCount &ref_count(const T *);
template<typename T> void destroy(const T *);
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
        
    void incref(T *ptr) {
        if (ptr) {
            ref_count(ptr).increment();
        }
    };
        
    void decref(T *ptr) {
        if (ptr) {
            ref_count(ptr).decrement();
            if (ref_count(ptr).is_zero()) {
                //std::cout << "Destroying " << ptr << ", " << live_objects << "\n";
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
        if (ptr && ref_count(ptr).is_zero()) {
            //std::cout << "Creating " << ptr << ", " << live_objects << "\n";
        }
        incref(ptr);
    }

    IntrusivePtr(const IntrusivePtr<T> &other) : ptr(other.ptr) {
        incref(ptr);
    }

    IntrusivePtr<T> &operator=(const IntrusivePtr<T> &other) {        
        // other can be inside of something owned by this
        T *temp = other.ptr;
        incref(temp);
        decref(ptr);
        ptr = temp;
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
