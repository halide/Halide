#ifndef TEST_SHARDING_H
#define TEST_SHARDING_H

// This file may be used by AOT tests, so it deliberately does not
// include Halide.h

#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

namespace Halide {
namespace Internal {
namespace Test {

// Support the environment variables are used by the GoogleTest framework
// to allow a large test to be 'sharded' into smaller pieces:
//
// - If TEST_SHARD_STATUS_FILE is not empty, we should create a file at that path
//   to indicate to the test framework that we support sharding. (Note that this
//   must be done even if the test does a [SKIP] and executes no tests.)
// - If TEST_TOTAL_SHARDS and TEST_SHARD_INDEX are defined, we should
//   split our work into TEST_TOTAL_SHARDS chunks, and only do the TEST_SHARD_INDEX-th
//   chunk on this run.
//
// The Halide buildbots don't (yet) make use of these, but some downstream consumers do.

class Sharder {
    // returns empty string (not null) if env var not found
    static std::string get_env(const char *v) {
        const char *r = getenv(v);
        if (!r) r = "";
        return r;
    }

    // returns 0 if env var not found
    static int32_t get_env_i32(const char *v) {
        return std::atoi(get_env(v).c_str());  // 0 if not found
    }

    const size_t total_shards, shard_index;

public:
    // Available publicly in case the test is skipped via [SKIP] --
    // even if the test runs nothing, we still need to write to this file
    // (if requested) to avoid making the external test framework unhappy.
    // (We don't need to call it when actually instantiating a Sharder.)
    static void accept_sharded_status() {
        std::string shard_status_file = get_env("TEST_SHARD_STATUS_FILE");
        if (!shard_status_file.empty()) {
            std::ofstream f(shard_status_file, std::ios::out | std::ios::binary);
            f << "sharder\n";
            f.flush();
            f.close();
        }
    }

    explicit Sharder()
        : total_shards(get_env_i32("TEST_TOTAL_SHARDS")),
          shard_index(get_env_i32("TEST_SHARD_INDEX")) {

        accept_sharded_status();
        if (total_shards != 0) {
            if (total_shards < 0 || shard_index < 0 || shard_index >= total_shards) {
                std::cerr << "Illegal values for sharding: total " << total_shards << " current " << shard_index << "\n";
                exit(1);
            }
        }
    }

    bool should_run(size_t task_index) const {
        if (total_shards > 0) {
            return (task_index % total_shards) == shard_index;
        } else {
            return true;
        }
    }

    bool is_sharded() const {
        return total_shards > 0;
    }
};

}  // namespace Test
}  // namespace Internal
}  // namespace Halide

#endif  // TEST_SHARDING_H
