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

    std::bitset<4> options;

    explicit SearchSpaceOptions(const std::string &bit_str)
        : options{bit_str} {
        aslog(1) << "Search space options:\n";
        aslog(1) << "Input string: " << bit_str << "\n";
        aslog(1) << "Compute root: " << compute_root() << "\n";
        aslog(1) << "Compute inline: " << compute_inline() << "\n";
        aslog(1) << "Compute at block: " << compute_at_block() << "\n";
        aslog(1) << "Compute at thread: " << compute_at_thread() << "\n";
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
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // SEARCH_SPACE_OPTIONS_H
