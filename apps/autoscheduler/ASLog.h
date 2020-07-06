#ifndef ASLOG_H
#define ASLOG_H

// This class is used by train_cost_model, which doesn't link to
// libHalide, so (despite the namespace) we are better off not
// including Halide.h, lest we reference something we won't have available

#include <cstdlib>
#include <iostream>
#include <utility>

namespace Halide {
namespace Internal {

class aslog {
    const bool logging;

public:
    aslog(int verbosity)
        : logging(verbosity <= aslog_level()) {
    }

    template<typename T>
    aslog &operator<<(T &&x) {
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
