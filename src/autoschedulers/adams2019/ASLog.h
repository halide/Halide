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

class aslog_streambuf : public std::streambuf {
public:
    explicit aslog_streambuf(bool do_log)
        : do_log_(do_log) {
    }

protected:
    std::streamsize xsputn(const char_type *s, std::streamsize n) override {
        if (do_log_) {
            std::cerr.write(s, n);
        }
        return n;  // returns the number of characters successfully written.
    };

    int_type overflow(int_type ch) override {
        if (do_log_) {
            std::cerr.put(ch);
        }
        return 1;  // returns the number of characters successfully written.
    }

private:
    const bool do_log_;
};

int aslog_level();

class aslog_stream : private aslog_streambuf, public std::ostream {
public:
    explicit aslog_stream(int verbosity)
        : aslog_streambuf(verbosity <= aslog_level()), std::ostream(this) {
    }

    // Not movable, not copyable.
    aslog_stream() = delete;
    aslog_stream(const aslog_stream &) = delete;
    aslog_stream &operator=(const aslog_stream &) = delete;
    aslog_stream(aslog_stream &&) = delete;
    aslog_stream &operator=(aslog_stream &&) = delete;
};

aslog_stream &aslog(int verbosity);

}  // namespace Internal
}  // namespace Halide

#endif
