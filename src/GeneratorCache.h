#ifndef HALIDE_GENERATOR_CACHE_H
#define HALIDE_GENERATOR_CACHE_H

/** \file
 *
 * Defines an opt-in, content-addressed cache for the artifacts produced by
 * running a Generator (object files, headers, static libraries, etc.). The
 * cache is enabled by setting the HL_CACHE_DIR environment variable; when it
 * is unset the machinery here is completely inert and has no effect on the
 * output of a build.
 *
 * A cache entry is keyed by a strong digest that captures everything which
 * can affect the emitted files: the serialized (scheduled) Halide pipeline,
 * the generator-param settings, the target(s), the requested output types,
 * and a fingerprint of the Halide compiler itself (see
 * compiler_identity_digest). If two invocations agree on all of these, they
 * must produce identical outputs, so the cached copies can be reused.
 */

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Module.h"  // for OutputFileType

namespace Halide {
namespace Internal {

/** Incremental builder for a generator-cache key. Each added field is
 * length-prefixed before being mixed in, so that distinct sequences of
 * fields can never produce the same digest by concatenation ambiguity. The
 * finished key is a lowercase hex-encoded SHA-256 digest. */
class CacheKeyBuilder {
public:
    CacheKeyBuilder();
    ~CacheKeyBuilder();
    CacheKeyBuilder(const CacheKeyBuilder &) = delete;
    CacheKeyBuilder &operator=(const CacheKeyBuilder &) = delete;

    /** Mix in an opaque field. */
    // @{
    CacheKeyBuilder &add(std::string_view data);
    CacheKeyBuilder &add(const std::vector<uint8_t> &data);
    // @}

    /** Mix in the contents of a file, streamed in chunks (so that hashing a
     * large shared library does not require loading it fully into memory).
     * Throws a user_error if the file cannot be read. */
    CacheKeyBuilder &add_file(const std::filesystem::path &path);

    /** Finalize and return a 64-character lowercase hex digest. Must be
     * called at most once per builder. */
    std::string hex();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

/** A hex SHA-256 of the binary backing libHalide (i.e. the "compiler
 * identity"). Mixing this into every cache key ensures that rebuilding
 * Halide (or LLVM) invalidates previously cached artifacts, since a new
 * compiler may lower the same pipeline differently. When libHalide is
 * statically linked this resolves to the generator executable itself, which
 * is equally valid. Computed once and memoized; returns "" (and disables
 * caching at the call site) if it cannot be determined. */
std::string compiler_identity_digest();

/** Opt-in, content-addressed cache for the set of files a Generator emits.
 * Active only when HL_CACHE_DIR names a directory. */
struct GeneratorCache {
    /** The configured cache directory, or "" when HL_CACHE_DIR is unset or
     * empty (i.e. caching disabled). Read once and memoized. */
    static std::string cache_dir();

    /** If a complete entry for `key` exists in the cache, copy each cached
     * file into the destination path given by `output_files` and return
     * true. Otherwise leave the filesystem untouched and return false. A
     * partial or corrupt entry is treated as a miss. */
    static bool try_restore(const std::string &key,
                            const std::map<OutputFileType, std::string> &output_files);

    /** Copy the already-emitted `output_files` into the cache under `key`
     * (installed atomically so concurrent builds never observe a partial
     * entry), then opportunistically prune the cache to honor the configured
     * size/age limits. A no-op when caching is disabled. */
    static void store(const std::string &key,
                      const std::map<OutputFileType, std::string> &output_files);
};

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_GENERATOR_CACHE_H
