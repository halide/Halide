#include "GeneratorCache.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/SHA256.h>

#include "Debug.h"
#include "Error.h"
#include "Util.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#elif defined(__linux__)
#include <elf.h>
#include <link.h>
#endif

namespace Halide {
namespace Internal {

namespace fs = std::filesystem;

namespace {

// Bump this whenever the on-disk cache layout changes in an incompatible way.
constexpr char kFormatVersion[] = "hlgc1";

// Default cache budget when HL_CACHE_MAX_SIZE is unset.
constexpr uint64_t kDefaultMaxSizeBytes = 1024ull * 1024 * 1024;  // 1 GiB

// Debounce interval for pruning: a build of N libraries runs N generator
// processes that each store and would each otherwise walk the whole cache. We
// scan at most once per this many seconds (the size limit is therefore a soft
// cap that a burst can briefly exceed). Concurrent processes are already
// serialized by the cache lock; this bounds the *sequential* case too.
constexpr int kPruneDebounceSecs = 60;

std::string to_hex(const std::array<uint8_t, 32> &digest) {
    static const char *chars = "0123456789abcdef";
    std::string out(64, '0');
    for (size_t i = 0; i < digest.size(); i++) {
        out[2 * i] = chars[digest[i] >> 4];
        out[2 * i + 1] = chars[digest[i] & 0xf];
    }
    return out;
}

// Parse a byte count that may carry a K/M/G suffix (powers of 1024).
// Returns `fallback` if the string is empty or unparsable.
uint64_t parse_size(const std::string &s, uint64_t fallback) {
    if (s.empty()) {
        return fallback;
    }
    size_t i = 0;
    uint64_t value = 0;
    bool any = false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        value = value * 10 + (s[i] - '0');
        any = true;
        i++;
    }
    if (!any) {
        return fallback;
    }
    uint64_t mult = 1;
    if (i < s.size()) {
        switch (std::tolower(static_cast<unsigned char>(s[i]))) {
        case 'k':
            mult = 1024ull;
            break;
        case 'm':
            mult = 1024ull * 1024;
            break;
        case 'g':
            mult = 1024ull * 1024 * 1024;
            break;
        default:
            break;
        }
    }
    return value * mult;
}

// Resolve the on-disk path of the binary that provides libHalide's code.
// When libHalide is statically linked this is the running executable.
fs::path libhalide_path() {
#ifdef _WIN32
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&compiler_identity_digest),
                            &module)) {
        return {};
    }
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(module, buf.data(), (DWORD)buf.size());
        if (n == 0) {
            return {};
        }
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return fs::path(buf);
#else
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(&compiler_identity_digest), &info) &&
        info.dli_fname != nullptr) {
        return fs::path(info.dli_fname);
    }
    return {};
#endif
}

// The linker-assigned build id of the binary providing libHalide, read from
// the already-loaded image (no file I/O). This changes on any relink, so it is
// a cheap stand-in for hashing the whole binary. Returns empty when no build id
// is available, in which case the caller falls back to a full hash.
std::vector<uint8_t> compiler_build_id() {
#if defined(__APPLE__)
    // Mach-O LC_UUID: a load command in the header, present by default and not
    // removed by strip.
    Dl_info info;
    if (!dladdr(reinterpret_cast<void *>(&compiler_identity_digest), &info) ||
        info.dli_fbase == nullptr) {
        return {};
    }
    const auto *mh = reinterpret_cast<const mach_header_64 *>(info.dli_fbase);
    if (mh->magic != MH_MAGIC_64) {
        return {};
    }
    const uint8_t *p = reinterpret_cast<const uint8_t *>(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const auto *lc = reinterpret_cast<const load_command *>(p);
        if (lc->cmd == LC_UUID) {
            const auto *uc = reinterpret_cast<const uuid_command *>(lc);
            return std::vector<uint8_t>(uc->uuid, uc->uuid + sizeof(uc->uuid));
        }
        p += lc->cmdsize;
    }
    return {};
#elif defined(__linux__)
    // ELF .note.gnu.build-id: an allocated NOTE (kept by strip), present when
    // the image was linked with --build-id (the common default).
    struct Ctx {
        const void *addr;
        std::vector<uint8_t> id;
    } ctx{reinterpret_cast<const void *>(&compiler_identity_digest), {}};

    dl_iterate_phdr(
        [](struct dl_phdr_info *info, size_t, void *data) -> int {
            auto *ctx = reinterpret_cast<Ctx *>(data);
            const auto want = reinterpret_cast<ElfW(Addr)>(ctx->addr);
            bool contains = false;
            for (int i = 0; i < info->dlpi_phnum; i++) {
                const ElfW(Phdr) &ph = info->dlpi_phdr[i];
                if (ph.p_type != PT_LOAD) {
                    continue;
                }
                const ElfW(Addr) start = info->dlpi_addr + ph.p_vaddr;
                if (want >= start && want < start + ph.p_memsz) {
                    contains = true;
                }
            }
            if (!contains) {
                return 0;  // not our image; keep looking
            }
            for (int i = 0; i < info->dlpi_phnum; i++) {
                const ElfW(Phdr) &ph = info->dlpi_phdr[i];
                if (ph.p_type != PT_NOTE) {
                    continue;
                }
                const uint8_t *p =
                    reinterpret_cast<const uint8_t *>(info->dlpi_addr + ph.p_vaddr);
                const uint8_t *end = p + ph.p_memsz;
                while (p + sizeof(ElfW(Nhdr)) <= end) {
                    const auto *n = reinterpret_cast<const ElfW(Nhdr) *>(p);
                    const uint8_t *name = p + sizeof(ElfW(Nhdr));
                    const uint8_t *desc = name + ((n->n_namesz + 3) & ~3u);
                    if (n->n_type == NT_GNU_BUILD_ID && n->n_namesz == 4 &&
                        std::memcmp(name, "GNU", 4) == 0 && desc + n->n_descsz <= end) {
                        ctx->id.assign(desc, desc + n->n_descsz);
                        return 1;
                    }
                    p = desc + ((n->n_descsz + 3) & ~3u);
                }
            }
            return 1;  // our image, but no build id
        },
        &ctx);
    return ctx.id;
#elif defined(_WIN32)
    // PE debug directory, CodeView (RSDS) entry: the linker-assigned PDB GUID
    // + age, present whenever the binary was built with debug info emitted
    // (the default for MSVC and clang-cl, with or without a shipped PDB) and
    // read straight from the mapped image, no file I/O required.
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&compiler_identity_digest),
                            &module)) {
        return {};
    }
    const auto *base = reinterpret_cast<const uint8_t *>(module);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return {};
    }
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return {};
    }
    const IMAGE_DATA_DIRECTORY &debug_entry =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (debug_entry.VirtualAddress == 0 || debug_entry.Size < sizeof(IMAGE_DEBUG_DIRECTORY)) {
        return {};
    }
    const auto *debug_dirs =
        reinterpret_cast<const IMAGE_DEBUG_DIRECTORY *>(base + debug_entry.VirtualAddress);
    const size_t count = debug_entry.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
    for (size_t i = 0; i < count; i++) {
        const IMAGE_DEBUG_DIRECTORY &dd = debug_dirs[i];
        if (dd.Type != IMAGE_DEBUG_TYPE_CODEVIEW || dd.AddressOfRawData == 0) {
            continue;
        }
        // RSDS (PDB70) layout: 4-byte signature, 16-byte GUID, 4-byte age,
        // then a NUL-terminated PDB path we don't need.
        const uint8_t *cv = base + dd.AddressOfRawData;
        if (dd.SizeOfData < 4 + 16 + 4 || std::memcmp(cv, "RSDS", 4) != 0) {
            continue;
        }
        return std::vector<uint8_t>(cv + 4, cv + 4 + 16 + 4);
    }
    return {};
#else
    // No known way to cheaply fingerprint the binary on this platform; fall
    // back to hashing the whole binary, which is correct (but slower).
    return {};
#endif
}

// A best-effort, advisory, whole-cache lock used to serialize pruning across
// concurrent generator processes. The lock is released automatically if the
// holding process exits, so a crash cannot wedge the cache. Acquisition is
// non-blocking: if another process holds it we simply skip our prune pass.
class CacheDirLock {
public:
    explicit CacheDirLock(const fs::path &cache_dir) {
        const fs::path lock_path = cache_dir / ".lock";
#ifdef _WIN32
        handle_ = CreateFileW(lock_path.wstring().c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            return;
        }
        OVERLAPPED ov = {};
        if (LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                       0, MAXDWORD, MAXDWORD, &ov)) {
            held_ = true;
        }
#else
        fd_ = ::open(lock_path.c_str(), O_RDWR | O_CREAT, 0666);
        if (fd_ < 0) {
            return;
        }
        if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
            held_ = true;
        }
#endif
    }

    ~CacheDirLock() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            if (held_) {
                OVERLAPPED ov = {};
                UnlockFileEx(handle_, 0, MAXDWORD, MAXDWORD, &ov);
            }
            CloseHandle(handle_);
        }
#else
        if (fd_ >= 0) {
            if (held_) {
                ::flock(fd_, LOCK_UN);
            }
            ::close(fd_);
        }
#endif
    }

    CacheDirLock(const CacheDirLock &) = delete;
    CacheDirLock &operator=(const CacheDirLock &) = delete;

    bool held() const {
        return held_;
    }

private:
    bool held_ = false;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
};

// Roots within the cache directory.
fs::path entries_root(const fs::path &cache_dir) {
    return cache_dir / "entries";
}

fs::path entry_dir(const fs::path &cache_dir, const std::string &key) {
    // Shard by the first two hex chars to keep any single directory small.
    return entries_root(cache_dir) / key.substr(0, 2) / key;
}

// A unique, same-filesystem staging directory so that the final install is a
// plain rename (atomic). Created under the cache dir, never the system temp.
fs::path make_staging_dir(const fs::path &cache_dir) {
    static std::atomic<uint64_t> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = ::getpid();
#endif
    for (int attempt = 0; attempt < 1000; attempt++) {
        fs::path candidate =
            cache_dir / "tmp" /
            ("stage-" + std::to_string((uint64_t)pid) + "-" +
             std::to_string((uint64_t)now) + "-" +
             std::to_string(counter.fetch_add(1)));
        std::error_code ec;
        if (fs::create_directories(candidate, ec) && !ec) {
            return candidate;
        }
    }
    return {};
}

// Touch (create/refresh) the last-used sentinel used by the LRU pruner.
void touch_used(const fs::path &dir) {
    std::error_code ec;
    const fs::path used = dir / ".used";
    { std::ofstream f(used, std::ios::binary | std::ios::app); }
    fs::last_write_time(used, fs::file_time_type::clock::now(), ec);
}

uint64_t dir_size(const fs::path &dir) {
    uint64_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) {
            break;
        }
        if (it->is_regular_file(ec)) {
            total += it->file_size(ec);
        }
    }
    return total;
}

// Enforce the configured size/age budget by evicting least-recently-used
// entries. Best-effort: runs under a non-blocking whole-cache lock and simply
// bails if it cannot be acquired or the cache directory can't be scanned.
void prune_cache(const fs::path &root) {
    const uint64_t max_size =
        parse_size(get_env_variable("HL_CACHE_MAX_SIZE"), kDefaultMaxSizeBytes);
    const uint64_t max_age_secs =
        parse_size(get_env_variable("HL_CACHE_MAX_AGE"), 0);

    CacheDirLock lock(root);
    if (!lock.held()) {
        // Another process is pruning; best-effort, so just skip.
        return;
    }

    // Skip the full directory walk if we pruned recently (debounce).
    const fs::path stamp = root / ".last_prune";
    std::error_code stamp_ec;
    const auto last_prune = fs::last_write_time(stamp, stamp_ec);
    if (!stamp_ec) {
        const auto since = std::chrono::duration_cast<std::chrono::seconds>(
                               fs::file_time_type::clock::now() - last_prune)
                               .count();
        if (since >= 0 && since < kPruneDebounceSecs) {
            return;
        }
    }

    struct Entry {
        fs::path path;
        uint64_t size = 0;
        fs::file_time_type used;
    };
    std::vector<Entry> entries;

    std::error_code ec;
    for (fs::directory_iterator shard(entries_root(root), ec), end; shard != end; shard.increment(ec)) {
        if (ec) {
            break;
        }
        if (!shard->is_directory(ec)) {
            continue;
        }
        std::error_code inner_ec;
        for (fs::directory_iterator e(shard->path(), inner_ec), eend; e != eend; e.increment(inner_ec)) {
            if (inner_ec) {
                break;
            }
            if (!e->is_directory(inner_ec)) {
                continue;
            }
            Entry entry;
            entry.path = e->path();
            entry.size = dir_size(entry.path);
            std::error_code used_ec;
            entry.used = fs::last_write_time(entry.path / ".used", used_ec);
            if (used_ec) {
                entry.used = fs::last_write_time(entry.path, used_ec);
            }
            entries.push_back(std::move(entry));
        }
    }

    const auto now = fs::file_time_type::clock::now();

    // Age-based eviction.
    if (max_age_secs > 0) {
        std::vector<Entry> keep;
        keep.reserve(entries.size());
        for (auto &entry : entries) {
            const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - entry.used).count();
            if (age >= 0 && (uint64_t)age > max_age_secs) {
                std::error_code rm_ec;
                fs::remove_all(entry.path, rm_ec);
            } else {
                keep.push_back(std::move(entry));
            }
        }
        entries.swap(keep);
    }

    // Size-based eviction: drop least-recently-used first.
    uint64_t total = 0;
    for (const auto &entry : entries) {
        total += entry.size;
    }
    if (total > max_size) {
        std::sort(entries.begin(), entries.end(),
                  [](const Entry &a, const Entry &b) { return a.used < b.used; });
        for (const auto &entry : entries) {
            if (total <= max_size) {
                break;
            }
            std::error_code rm_ec;
            fs::remove_all(entry.path, rm_ec);
            total -= std::min(total, entry.size);
        }
    }

    // Record that a prune happened so the debounce above can skip the next few.
    { std::ofstream f(stamp, std::ios::binary | std::ios::app); }
    fs::last_write_time(stamp, fs::file_time_type::clock::now(), stamp_ec);
}

}  // namespace

struct CacheKeyBuilder::Impl {
    llvm::SHA256 hasher;
};

CacheKeyBuilder::CacheKeyBuilder()
    : impl(new Impl) {
}

CacheKeyBuilder::~CacheKeyBuilder() = default;

CacheKeyBuilder &CacheKeyBuilder::add(std::string_view data) {
    // Length-prefix so that add("ab")+add("c") differs from add("a")+add("bc").
    const uint64_t len = data.size();
    impl->hasher.update(llvm::ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(&len), sizeof(len)));
    impl->hasher.update(llvm::StringRef(data.data(), data.size()));
    return *this;
}

CacheKeyBuilder &CacheKeyBuilder::add(const std::vector<uint8_t> &data) {
    const uint64_t len = data.size();
    impl->hasher.update(llvm::ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(&len), sizeof(len)));
    impl->hasher.update(llvm::ArrayRef<uint8_t>(data.data(), data.size()));
    return *this;
}

CacheKeyBuilder &CacheKeyBuilder::add_file(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        user_error << "GeneratorCache: unable to read file for hashing: " << path.string() << "\n";
    }
    std::error_code ec;
    const uint64_t len = fs::file_size(path, ec);
    impl->hasher.update(llvm::ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(&len), sizeof(len)));
    std::vector<uint8_t> buf(1 << 16);
    while (f) {
        f.read(reinterpret_cast<char *>(buf.data()), buf.size());
        const std::streamsize got = f.gcount();
        if (got > 0) {
            impl->hasher.update(llvm::ArrayRef<uint8_t>(buf.data(), (size_t)got));
        }
    }
    return *this;
}

std::string CacheKeyBuilder::hex() {
    return to_hex(impl->hasher.final());
}

std::string compiler_identity_digest() {
    static const std::string digest = []() -> std::string {
        CacheKeyBuilder b;
        b.add("compiler-identity");

        // Prefer the linker-assigned build id: it changes on any relink and is
        // read straight from the loaded image, avoiding a hash of the whole
        // (tens of MB) binary once per generator process.
        const std::vector<uint8_t> id = compiler_build_id();
        if (!id.empty()) {
            b.add("build-id");
            b.add(id);
            return b.hex();
        }

        // Fall back to hashing the entire binary when no build id is available
        // (e.g. a Linux image linked with --build-id=none, or a Windows image
        // built without debug info). This is equally correct, just slower.
        const fs::path path = libhalide_path();
        if (path.empty()) {
            debug(1) << "GeneratorCache: could not locate libHalide to fingerprint; "
                        "caching disabled.\n";
            return "";
        }
        b.add("file");
        b.add_file(path);
        return b.hex();
    }();
    return digest;
}

std::string GeneratorCache::cache_dir() {
    static const std::string dir = get_env_variable("HL_CACHE_DIR");
    return dir;
}

bool GeneratorCache::try_restore(const std::string &key,
                                 const std::map<OutputFileType, std::string> &output_files) {
    const std::string dir = cache_dir();
    if (dir.empty() || key.empty()) {
        return false;
    }

    const fs::path root = dir;
    const fs::path entry = entry_dir(root, key);
    std::error_code ec;

    // A committed entry always has a manifest with a matching format version.
    std::ifstream manifest(entry / "manifest.txt", std::ios::binary);
    if (!manifest) {
        return false;
    }
    std::string version;
    std::getline(manifest, version);
    if (version != kFormatVersion) {
        return false;
    }

    // Every requested output must have a blob before we commit to a restore.
    for (const auto &kv : output_files) {
        const fs::path blob = entry / ("f" + std::to_string((int)kv.first));
        if (!fs::exists(blob, ec)) {
            return false;
        }
    }

    // Copy into temporaries first, then rename into place, so a failure part
    // way through never leaves the build with a half-written output file.
    std::vector<std::pair<fs::path, fs::path>> pending;  // (tmp, dest)
    for (const auto &kv : output_files) {
        const fs::path blob = entry / ("f" + std::to_string((int)kv.first));
        const fs::path dest = kv.second;
        const fs::path tmp = dest.string() + ".hlcache.tmp";
        fs::create_directories(dest.parent_path(), ec);
        fs::copy_file(blob, tmp, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            for (const auto &p : pending) {
                fs::remove(p.first, ec);
            }
            fs::remove(tmp, ec);
            return false;
        }
        pending.emplace_back(tmp, dest);
    }
    for (const auto &p : pending) {
        fs::rename(p.first, p.second, ec);
        if (ec) {
            // Fall back to a copy if rename across the tmp/dest pair failed.
            fs::copy_file(p.first, p.second, fs::copy_options::overwrite_existing, ec);
            fs::remove(p.first, ec);
        }
    }

    touch_used(entry);
    debug(1) << "GeneratorCache: hit for key " << key << "\n";
    return true;
}

void GeneratorCache::store(const std::string &key,
                           const std::map<OutputFileType, std::string> &output_files) {
    const std::string dir = cache_dir();
    if (dir.empty() || key.empty()) {
        return;
    }
    const fs::path root = dir;
    std::error_code ec;

    const fs::path entry = entry_dir(root, key);
    if (fs::exists(entry / "manifest.txt", ec)) {
        // Already cached (possibly by a concurrent build). Nothing to do.
        return;
    }

    const fs::path staging = make_staging_dir(root);
    if (staging.empty()) {
        debug(1) << "GeneratorCache: could not create staging dir; skipping store.\n";
        return;
    }

    std::ofstream manifest(staging / "manifest.txt", std::ios::binary);
    manifest << kFormatVersion << "\n";
    for (const auto &kv : output_files) {
        const fs::path src = kv.second;
        if (!fs::exists(src, ec)) {
            // An expected output is missing; don't cache an incomplete entry.
            debug(1) << "GeneratorCache: output missing at store time (" << src
                     << "); skipping store.\n";
            manifest.close();
            fs::remove_all(staging, ec);
            return;
        }
        const int type = (int)kv.first;
        fs::copy_file(src, staging / ("f" + std::to_string(type)),
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            debug(1) << "GeneratorCache: failed to stage " << src << "; skipping store.\n";
            manifest.close();
            fs::remove_all(staging, ec);
            return;
        }
        manifest << type << " f" << type << "\n";
    }
    manifest.close();
    touch_used(staging);

    fs::create_directories(entry.parent_path(), ec);
    fs::rename(staging, entry, ec);
    if (ec) {
        // Lost a race (entry now exists) or a cross-filesystem rename: clean up.
        fs::remove_all(staging, ec);
        return;
    }

    debug(1) << "GeneratorCache: stored key " << key << "\n";

    prune_cache(root);
}

}  // namespace Internal
}  // namespace Halide
