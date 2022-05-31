#ifndef TEST_SHARDING_H
#define TEST_SHARDING_H

// This file may be used by AOT tests, so it deliberately does not
// include Halide.h

#include <cassert>
#include <fstream>
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
    static std::string get_env(const char *v) {
        const char *r = getenv(v);
        if (!r) r = "";
        return r;
    }

    size_t sharded_first, sharded_last;
    bool sharded;

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

    explicit Sharder(size_t num_tasks) : sharded_first(0), sharded_last(num_tasks - 1), sharded(false) {
        accept_sharded_status();

        int total_shards = std::atoi(get_env("TEST_TOTAL_SHARDS").c_str());  // 0 if not present
        int current_shard = std::atoi(get_env("TEST_SHARD_INDEX").c_str());  // 0 if not present

        if (total_shards != 0) {
            if (total_shards < 0 || current_shard < 0 || current_shard >= total_shards) {
                std::cerr << "Illegal values for sharding: total " << total_shards << " current " << current_shard << "\n";
                exit(-1);
            }

            size_t shard_size = (num_tasks + total_shards - 1) / total_shards;
            sharded_first = current_shard * shard_size;
            sharded_last = std::min(sharded_first + shard_size - 1, num_tasks - 1);
            sharded = true;
            // Useful for debugging
            // std::cout << "Tasks " << tasks.size() << " shard_size " << shard_size << " sharded_first " << sharded_first << " sharded_last " << sharded_last << "\n";
        }
    }

    size_t first() const { return sharded_first; }
    size_t last() const { return sharded_last; }
    bool is_sharded() const { return sharded; }
};

}  // namespace Test
}  // namespace Internal
}  // namespace Halide

#endif  // TEST_SHARDING_H
