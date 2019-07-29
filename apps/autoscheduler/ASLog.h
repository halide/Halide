#ifndef ASLOG_H
#define ASLOG_H

#include "Halide.h"

namespace Halide {
namespace Internal {

class aslog {
    const bool logging;

public:
    aslog(int verbosity) : logging(verbosity <= aslog_level()) {}

    template<typename T>
    aslog &operator<<(T&& x) {
        if (logging) {
            std::cerr << std::forward<T>(x);
        }
        return *this;
    }

    static int aslog_level();
};

}  // namespace Internal
}  // namespace Halide

#endif
