#ifndef SEARCH_SPACE_OPTIONS_H
#define SEARCH_SPACE_OPTIONS_H

#include <bitset>

#include "ASLog.h"

namespace Halide {
namespace Internal {
namespace Autoscheduler {

struct SearchSpaceOptions {
    constexpr static size_t option_compute_root = 0;
    constexpr static size_t option_compute_inline = 1;
    constexpr static size_t option_compute_at_block = 2;
    constexpr static size_t option_compute_at_thread = 3;
    constexpr static size_t option_serial_splits_after_compute_at = 4;

    std::bitset<5> options;

    SearchSpaceOptions(const std::string& bit_str)
        : options{bit_str}
    {
        aslog(0) << "Search space options:\n";
        aslog(0) << "Input string: " << bit_str << "\n";
        aslog(0) << "Compute root: " << compute_root() << "\n";
        aslog(0) << "Compute inline: " << compute_inline() << "\n";
        aslog(0) << "Compute at block: " << compute_at_block() << "\n";
        aslog(0) << "Compute at thread: " << compute_at_thread() << "\n";
        aslog(0) << "Serial splits afting compute_at: " << serial_splits_after_compute_at() << "\n";
    }

    bool compute_root() const {
        return options.test(SearchSpaceOptions::option_compute_root) || compute_at_block() || compute_at_thread();
    }

    bool compute_root_only() const {
        return options.count() == 1 && compute_root();
    }

    bool compute_inline() const {
        return options.test(SearchSpaceOptions::option_compute_inline);
    }

    bool compute_inline_only() const {
        return options.count() == 1 && compute_inline();
    }

    bool compute_at_block() const {
        return options.test(SearchSpaceOptions::option_compute_at_block);
    }

    bool compute_at_block_only() const {
        return options.count() == 1 && compute_at_block();
    }

    bool compute_at_thread() const {
        return options.test(SearchSpaceOptions::option_compute_at_thread);
    }

    bool compute_at_thread_only() const {
        return options.count() == 1 && compute_at_thread();
    }

    bool serial_splits_after_compute_at() const {
        return options.test(SearchSpaceOptions::option_serial_splits_after_compute_at);
    }
};


}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif // SEARCH_SPACE_OPTIONS_H
