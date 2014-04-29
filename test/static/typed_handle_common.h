#ifndef TYPED_HANDLE_ARGUMENT_COMMON_H
#define TYPED_HANDLE_ARGUMENT_COMMON_H

#include <string>

#include <HalideRuntime.h>

class TypedHandle {
    uint8_t value;
public:
    TypedHandle(uint8_t init) : value(init) { }
    uint8_t get() const { return value; };
};

template<>
struct halide_handle_traits<TypedHandle> {
    static const std::string type_name() { return "TypedHandle"; }
};

extern "C" uint8_t typed_handle_get(const TypedHandle *handle) {
  return handle->get();
}

#endif
