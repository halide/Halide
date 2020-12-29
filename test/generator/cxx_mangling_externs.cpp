#include <cstdint>

// These are the HalideExtern functions referenced by cxx_mangling_generator.cpp
int32_t extract_value_global(int32_t *arg) {
    return *arg;
}

namespace HalideTest {

int32_t extract_value_ns(const int32_t *arg) {
    return *arg;
}

}  // namespace HalideTest
