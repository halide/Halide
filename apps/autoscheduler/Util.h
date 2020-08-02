#ifndef UTIL_H
#define UTIL_H

#include <memory>

namespace Halide {
namespace Internal {
namespace Autoscheduler {

template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args &&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif
