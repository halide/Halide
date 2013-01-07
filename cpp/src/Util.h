#ifndef HALIDE_UTIL_H
#define HALIDE_UTIL_H

#include <vector>
#include <string>

/* This file contains various utility functions for Halide. */

namespace Halide { 
namespace Internal {

/* Build small vectors of up to 6 elements. If we used C++11 and
 * had vector initializers, this would not be necessary, but we
 * don't want to rely on C++11 support. */
template<typename T>
std::vector<T> vec(T a) {
    std::vector<T> v(1);
    v[0] = a;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b) {
    std::vector<T> v(2);
    v[0] = a;
    v[1] = b;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c) {
    std::vector<T> v(3);
    v[0] = a;
    v[1] = b;
    v[2] = c;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c, T d) {
    std::vector<T> v(4);
    v[0] = a;        
    v[1] = b;
    v[2] = c;
    v[3] = d;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c, T d, T e) {
    std::vector<T> v(5);
    v[0] = a;        
    v[1] = b;
    v[2] = c;
    v[3] = d;
    v[4] = e;
    return v;
}

template<typename T>
std::vector<T> vec(T a, T b, T c, T d, T e, T f) {
    std::vector<T> v(6);
    v[0] = a;        
    v[1] = b;
    v[2] = c;
    v[3] = d;
    v[4] = e;
    v[5] = f;
    return v;
}

// Generate a unique name starting with the given character
std::string unique_name(char prefix);

bool starts_with(const std::string &str, const std::string &prefix);
bool ends_with(const std::string &str, const std::string &suffix);

}
}

#endif
