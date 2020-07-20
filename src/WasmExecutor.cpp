#include "WasmExecutor.h"

#include "CodeGen_WebAssembly.h"
#include "Error.h"
#include "Float16.h"
#include "Func.h"
#include "ImageParam.h"
#include "JITModule.h"
#if WITH_WABT
#include "LLVM_Headers.h"
#endif
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"
#include "Target.h"

#include <cmath>
#include <csignal>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

#if WITH_WABT
#include "wabt-src/src/binary-reader.h"
#include "wabt-src/src/error-formatter.h"
#include "wabt-src/src/feature.h"
#include "wabt-src/src/interp/binary-reader-interp.h"
#include "wabt-src/src/interp/interp-util.h"
#include "wabt-src/src/interp/interp-wasi.h"
#include "wabt-src/src/interp/interp.h"
#include "wabt-src/src/option-parser.h"
#include "wabt-src/src/stream.h"
#endif

namespace Halide {
namespace Internal {

// Trampolines do not use "_argv" as the suffix because
// that name may already exist and if so, will return an int
// instead of taking a pointer at the end of the args list to
// receive the result value.
static const char kTrampolineSuffix[] = "_trampoline";

#if WITH_WABT

namespace {

// ---------------------
// General debug helpers
// ---------------------

// Debugging the WebAssembly JIT support is usually disconnected from the rest of HL_DEBUG_CODEGEN
#define WASM_DEBUG_LEVEL 0

struct debug_sink {
    debug_sink() = default;

    template<typename T>
    inline debug_sink &operator<<(T &&x) {
        return *this;
    }
};

#if WASM_DEBUG_LEVEL
#define wdebug(x) Halide::Internal::debug(((x) <= WASM_DEBUG_LEVEL) ? 0 : 255)
#define wassert(x) internal_assert(x)
#else
#define wdebug(x) debug_sink()
#define wassert(x) debug_sink()
#endif

// ---------------------
// BDMalloc
// ---------------------

template<typename T>
inline T align_up(T p, int alignment = 32) {
    return (p + alignment - 1) & ~(alignment - 1);
}

// Debugging our Malloc is extremely noisy and usually undesired

#define BDMALLOC_DEBUG_LEVEL 0
#if BDMALLOC_DEBUG_LEVEL
#define bddebug(x) Halide::Internal::debug(((x) <= BDMALLOC_DEBUG_LEVEL) ? 0 : 255)
#else
#define bddebug(x) debug_sink()
#endif

// BDMalloc aka BrainDeadMalloc. This is an *extremely* simple-minded implementation
// of malloc/free on top of a WasmMemoryObject, and is intended to be just barely adequate
// to allow Halide's JIT-based tests to pass. It is neither memory-efficient nor performant,
// nor has it been particularly well-vetted for potential buffer overruns and such.
class BDMalloc {
    struct Region {
        uint32_t size : 31;
        uint32_t used : 1;
    };

    uint32_t total_size = 0;
    std::map<uint32_t, Region> regions;

public:
    BDMalloc() = default;

    uint32_t get_total_size() const {
        return total_size;
    }

    bool inited() const {
        return total_size > 0;
    }

    void init(uint32_t total_size, uint32_t heap_start = 1) {
        this->total_size = total_size;
        regions.clear();

        internal_assert(heap_start < total_size);
        // Area before heap_start is permanently off-limits
        regions[0] = {heap_start, true};
        // Everything else is free
        regions[heap_start] = {total_size - heap_start, false};
    }

    void reset() {
        this->total_size = 0;
        regions.clear();
    }

    uint32_t alloc_region(uint32_t requested_size) {
        internal_assert(requested_size > 0);

        bddebug(1) << "begin alloc_region " << requested_size << "\n";
        validate();

        // TODO: this would be faster with a basic free list,
        // but for most Halide test code, there aren't enough allocations
        // for this to be worthwhile, or at least that's my observation from
        // a first run-thru. Consider adding it if any of the JIT-based tests
        // seem unreasonably slow; as it is, a linear search for the first free
        // block of adequate size is (apparently) performant enough.

        // alignment and min-block-size are the same for our purposes here.
        constexpr uint32_t kAlignment = 32;
        const uint32_t size = std::max(align_up((uint32_t)requested_size, kAlignment), kAlignment);

        constexpr uint32_t kMaxAllocSize = 0x7fffffff;
        internal_assert(size <= kMaxAllocSize);
        bddebug(2) << "size -> " << size << "\n";

        for (auto it = regions.begin(); it != regions.end(); it++) {
            const uint32_t start = it->first;
            Region &r = it->second;
            if (!r.used && r.size >= size) {
                bddebug(2) << "alloc @ " << start << "," << (uint32_t)r.size << "\n";
                if (r.size > size + kAlignment) {
                    // Split the block
                    const uint32_t r2_start = start + size;
                    const uint32_t r2_size = r.size - size;
                    regions[r2_start] = {r2_size, false};
                    r.size = size;
                    bddebug(2) << "split: r-> " << start << "," << (uint32_t)r.size << "," << (start + r.size) << "\n";
                    bddebug(2) << "split: r2-> " << r2_start << "," << r2_size << "," << (r2_start + r2_size) << "\n";
                }
                // Just return the block
                r.used = true;
                bddebug(1) << "end alloc_region " << requested_size << "\n";
                validate();
                return start;
            }
        }
        bddebug(1) << "fail alloc_region " << requested_size << "\n";
        validate();
        return 0;
    }

    void free_region(uint32_t start) {
        bddebug(1) << "begin free_region " << start << "\n";
        validate();

        // Can't free region at zero
        if (!start) {
            return;
        }

        internal_assert(start > 0);
        auto it = regions.find(start);
        internal_assert(it != regions.end());
        internal_assert(it->second.used);
        it->second.used = false;
        // If prev region is free, combine with it
        if (it != regions.begin()) {
            auto prev = std::prev(it);
            if (!prev->second.used) {
                bddebug(2) << "combine prev: " << prev->first << " w/ " << it->first << "\n";
                prev->second.size += it->second.size;
                regions.erase(it);
                it = prev;
            }
        }
        // If next region is free, combine with it
        auto next = std::next(it);
        if (next != regions.end() && !next->second.used) {
            bddebug(2) << "combine next: " << next->first << " w/ " << it->first << " "
                       << "\n";
            it->second.size += next->second.size;
            regions.erase(next);
        }

        bddebug(1) << "end free_region " << start << "\n";
        validate();
    }

    void grow_total_size(uint32_t new_total_size) {
        bddebug(1) << "begin grow_total_size " << new_total_size << "\n";
        validate();

        internal_assert(new_total_size > total_size);
        auto it = regions.rbegin();
        const uint32_t start = it->first;
        Region &r = it->second;
        uint32_t r_end = start + r.size;
        internal_assert(r_end == total_size);
        uint32_t delta = new_total_size - r_end;
        if (r.used) {
            // Add a free region after the last one
            regions[r_end] = {delta, false};
        } else {
            // Just extend the last (free) region
            r.size += delta;
        }

        // bookkeeping
        total_size = new_total_size;

        bddebug(1) << "end grow_total_size " << new_total_size << "\n";
        validate();
    }

    void validate() const {
        internal_assert(total_size > 0);
#if (BDMALLOC_DEBUG_LEVEL >= 1) || (WASM_DEBUG_LEVEL >= 1)
        uint32_t prev_end = 0;
        bool prev_used = false;
        for (auto it : regions) {
            const uint32_t start = it.first;
            const Region &r = it.second;
            bddebug(2) << "R: " << start << ".." << (start + r.size - 1) << "," << r.used << "\n";
            wassert(start == prev_end) << "start " << start << " prev_end " << prev_end << "\n";
            // it's OK to have two used regions in a row, but not two free ones
            wassert(!(!prev_used && !r.used));
            prev_end = start + r.size;
            prev_used = r.used;
        }
        wassert(prev_end == total_size) << "prev_end " << prev_end << " total_size " << total_size << "\n";
        bddebug(2) << "\n";
#endif
    }
};

// ---------------------
// General Wasm helpers
// ---------------------

using wasm32_ptr_t = int32_t;

const wasm32_ptr_t kMagicJitUserContextValue = -1;

// TODO: vector codegen can underead allocated buffers; we need to deliberately
// allocate extra and return a pointer partway in to avoid out-of-bounds access
// failures. https://github.com/halide/Halide/issues/3738
constexpr size_t kExtraMallocSlop = 32;

std::vector<char> compile_to_wasm(const Module &module, const std::string &fn_name) {
    static std::mutex link_lock;
    std::lock_guard<std::mutex> lock(link_lock);

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> fn_module;

    // Default wasm stack size is ~64k, but schedules with lots of
    // alloca usage (heavily inlined, or tracing enabled) can blow thru
    // this, which crashes in amusing ways, so ask for extra stack space
    // for the alloca usage.
    size_t stack_size = 65536;
    {
        std::unique_ptr<CodeGen_WebAssembly> cg(new CodeGen_WebAssembly(module.target()));
        cg->set_context(context);
        fn_module = cg->compile(module);
        stack_size += cg->get_requested_alloca_total();
    }

    stack_size = align_up(stack_size);
    wdebug(1) << "Requesting stack size of " << stack_size << "\n";

    std::unique_ptr<llvm::Module> llvm_module =
        link_with_wasm_jit_runtime(&context, module.target(), std::move(fn_module));

    llvm::SmallVector<char, 4096> object;
    llvm::raw_svector_ostream object_stream(object);
    compile_llvm_module_to_object(*llvm_module, object_stream);

    // TODO: surely there's a better way that doesn't require spooling things
    // out to temp files
    TemporaryFile obj_file("", ".o");
    write_entire_file(obj_file.pathname(), object.data(), object.size());
#if WASM_DEBUG_LEVEL
    obj_file.detach();
    wdebug(1) << "Dumping obj_file to " << obj_file.pathname() << "\n";
#endif

    TemporaryFile wasm_output("", ".wasm");

    std::string lld_arg_strs[] = {
        "HalideJITLinker",
        // For debugging purposes:
        // "--verbose",
        // "-error-limit=0",
        // "--print-gc-sections",
        "--export=__heap_base",
        "--allow-undefined",
        "-zstack-size=" + std::to_string(stack_size),
        obj_file.pathname(),
        "--entry=" + fn_name,
        "-o",
        wasm_output.pathname()};

    constexpr int c = sizeof(lld_arg_strs) / sizeof(lld_arg_strs[0]);
    const char *lld_args[c];
    for (int i = 0; i < c; ++i) {
        lld_args[i] = lld_arg_strs[i].c_str();
    }

    // lld will temporarily hijack the signal handlers to ensure that temp files get cleaned up,
    // but rather than preserving custom handlers in place, it restores the default handlers.
    // This conflicts with some of our testing infrastructure, which relies on a SIGABRT handler
    // set at global-ctor time to stay set. Therefore we'll save and restore this ourselves.
    // Note that we must restore it before using internal_error (and also on the non-error path).
    auto old_abort_handler = std::signal(SIGABRT, SIG_DFL);

#if LLVM_VERSION >= 110
    if (!lld::wasm::link(lld_args, /*CanExitEarly*/ false, llvm::outs(), llvm::errs())) {
        std::signal(SIGABRT, old_abort_handler);
        internal_error << "lld::wasm::link failed\n";
    }
#elif LLVM_VERSION >= 100
    std::string lld_errs_string;
    llvm::raw_string_ostream lld_errs(lld_errs_string);

    if (!lld::wasm::link(lld_args, /*CanExitEarly*/ false, llvm::outs(), llvm::errs())) {
        std::signal(SIGABRT, old_abort_handler);
        internal_error << "lld::wasm::link failed: (" << lld_errs.str() << ")\n";
    }
#else
    std::string lld_errs_string;
    llvm::raw_string_ostream lld_errs(lld_errs_string);

    if (!lld::wasm::link(lld_args, /*CanExitEarly*/ false, lld_errs)) {
        std::signal(SIGABRT, old_abort_handler);
        internal_error << "lld::wasm::link failed: (" << lld_errs.str() << ")\n";
    }
#endif

    std::signal(SIGABRT, old_abort_handler);

#if WASM_DEBUG_LEVEL
    wasm_output.detach();
    wdebug(1) << "Dumping linked wasm to " << wasm_output.pathname() << "\n";
#endif

    return read_entire_file(wasm_output.pathname());
}

inline constexpr int halide_type_code(halide_type_code_t code, int bits) {
    return ((int)code) | (bits << 8);
}

// dynamic_type_dispatch is a utility for functors that want to be able
// to dynamically dispatch a halide_type_t to type-specialized code.
// To use it, a functor must be a *templated* class, e.g.
//
//     template<typename T> class MyFunctor { int operator()(arg1, arg2...); };
//
// dynamic_type_dispatch() is called with a halide_type_t as the first argument,
// followed by the arguments to the Functor's operator():
//
//     auto result = dynamic_type_dispatch<MyFunctor>(some_halide_type, arg1, arg2);
//
// Note that this means that the functor must be able to instantiate its
// operator() for all the Halide scalar types; it also means that all those
// variants *will* be instantiated (increasing code size), so this approach
// should only be used when strictly necessary.

// clang-format off
template<template<typename> class Functor, typename... Args>
auto dynamic_type_dispatch(const halide_type_t &type, Args &&... args) -> decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...)) {

#define HANDLE_CASE(CODE, BITS, TYPE)  \
    case halide_type_code(CODE, BITS): \
        return Functor<TYPE>()(std::forward<Args>(args)...);

    switch (halide_type_code((halide_type_code_t)type.code, type.bits)) {
        HANDLE_CASE(halide_type_bfloat, 16, bfloat16_t)
        HANDLE_CASE(halide_type_float, 16, float16_t)
        HANDLE_CASE(halide_type_float, 32, float)
        HANDLE_CASE(halide_type_float, 64, double)
        HANDLE_CASE(halide_type_int, 8, int8_t)
        HANDLE_CASE(halide_type_int, 16, int16_t)
        HANDLE_CASE(halide_type_int, 32, int32_t)
        HANDLE_CASE(halide_type_int, 64, int64_t)
        HANDLE_CASE(halide_type_uint, 1, bool)
        HANDLE_CASE(halide_type_uint, 8, uint8_t)
        HANDLE_CASE(halide_type_uint, 16, uint16_t)
        HANDLE_CASE(halide_type_uint, 32, uint32_t)
        HANDLE_CASE(halide_type_uint, 64, uint64_t)
        HANDLE_CASE(halide_type_handle, 64, void *)
    default:
        internal_error;
        using ReturnType = decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...));
        return ReturnType();
    }

#undef HANDLE_CASE
}
// clang-format on

std::string to_string(const wabt::MemoryStream &m) {
    // TODO: ugh
    wabt::OutputBuffer &o = const_cast<wabt::MemoryStream *>(&m)->output_buffer();
    return std::string((const char *)o.data.data(), o.data.size());
}

struct WabtContext {
    JITUserContext *const jit_user_context;
    wabt::interp::Memory &memory;
    BDMalloc &bdmalloc;

    explicit WabtContext(JITUserContext *jit_user_context, wabt::interp::Memory &memory, BDMalloc &bdmalloc)
        : jit_user_context(jit_user_context), memory(memory), bdmalloc(bdmalloc) {
    }

    WabtContext(const WabtContext &) = delete;
    WabtContext(WabtContext &&) = delete;
    void operator=(const WabtContext &) = delete;
    void operator=(WabtContext &&) = delete;
};

WabtContext &get_wabt_context(wabt::interp::Thread &thread) {
    void *host_info = thread.host_info();
    wassert(host_info);
    return *(WabtContext *)host_info;
}

uint8_t *get_wasm_memory_base(WabtContext &wabt_context) {
    return wabt_context.memory.UnsafeData();
}

wasm32_ptr_t wabt_malloc(WabtContext &wabt_context, size_t size) {
    wasm32_ptr_t p = wabt_context.bdmalloc.alloc_region(size);
    if (!p) {
        constexpr int kWasmPageSize = 65536;
        const int32_t pages_needed = (size + kWasmPageSize - 1) / 65536;
        wdebug(1) << "attempting to grow by pages: " << pages_needed << "\n";

        wabt::Result r = wabt_context.memory.Grow(pages_needed);
        internal_assert(Succeeded(r)) << "Memory::Grow() failed";

        wabt_context.bdmalloc.grow_total_size(wabt_context.memory.ByteSize());
        p = wabt_context.bdmalloc.alloc_region(size);
    }

    wdebug(2) << "allocation of " << size << " at: " << p << "\n";
    return p;
}

void wabt_free(WabtContext &wabt_context, wasm32_ptr_t ptr) {
    wdebug(2) << "freeing ptr at: " << ptr << "\n";
    wabt_context.bdmalloc.free_region(ptr);
}

// Some internal code can call halide_error(null, ...), so this needs to be resilient to that.
// Callers must expect null and not crash.
JITUserContext *get_jit_user_context(WabtContext &wabt_context, const wabt::interp::Value &arg) {
    int32_t ucon_magic = arg.Get<int32_t>();
    if (ucon_magic == 0) {
        return nullptr;
    }
    wassert(ucon_magic == kMagicJitUserContextValue);
    JITUserContext *jit_user_context = wabt_context.jit_user_context;
    wassert(jit_user_context);
    return jit_user_context;
}

// -----------------------
// halide_buffer_t <-> wasm_halide_buffer_t helpers
// -----------------------

struct wasm_halide_buffer_t {
    uint64_t device;
    wasm32_ptr_t device_interface;  // halide_device_interface_t*
    wasm32_ptr_t host;              // uint8_t*
    uint64_t flags;
    halide_type_t type;
    int32_t dimensions;
    wasm32_ptr_t dim;      // halide_dimension_t*
    wasm32_ptr_t padding;  // always zero
};

void dump_hostbuf(WabtContext &wabt_context, const halide_buffer_t *buf, const std::string &label) {
#if WASM_DEBUG_LEVEL >= 2
    const halide_dimension_t *dim = buf->dim;
    const uint8_t *host = buf->host;

    wdebug(1) << label << " = " << (void *)buf << " = {\n";
    wdebug(1) << "  device = " << buf->device << "\n";
    wdebug(1) << "  device_interface = " << buf->device_interface << "\n";
    wdebug(1) << "  host = " << (void *)host << " = {\n";
    if (host) {
        wdebug(1) << "    " << (int)host[0] << ", " << (int)host[1] << ", " << (int)host[2] << ", " << (int)host[3] << "...\n";
    }
    wdebug(1) << "  }\n";
    wdebug(1) << "  flags = " << buf->flags << "\n";
    wdebug(1) << "  type = " << (int)buf->type.code << "," << (int)buf->type.bits << "," << buf->type.lanes << "\n";
    wdebug(1) << "  dimensions = " << buf->dimensions << "\n";
    wdebug(1) << "  dim = " << (void *)buf->dim << " = {\n";
    for (int i = 0; i < buf->dimensions; i++) {
        const auto &d = dim[i];
        wdebug(1) << "    {" << d.min << "," << d.extent << "," << d.stride << "," << d.flags << "},\n";
    }
    wdebug(1) << "  }\n";
    wdebug(1) << "  padding = " << buf->padding << "\n";
    wdebug(1) << "}\n";
#endif
}

void dump_wasmbuf(WabtContext &wabt_context, wasm32_ptr_t buf_ptr, const std::string &label) {
#if WASM_DEBUG_LEVEL >= 2
    wassert(buf_ptr);

    uint8_t *base = get_wasm_memory_base(wabt_context);
    wasm_halide_buffer_t *buf = (wasm_halide_buffer_t *)(base + buf_ptr);
    halide_dimension_t *dim = buf->dim ? (halide_dimension_t *)(base + buf->dim) : nullptr;
    uint8_t *host = buf->host ? (base + buf->host) : nullptr;

    wdebug(1) << label << " = " << buf_ptr << " -> " << (void *)buf << " = {\n";
    wdebug(1) << "  device = " << buf->device << "\n";
    wdebug(1) << "  device_interface = " << buf->device_interface << "\n";
    wdebug(1) << "  host = " << buf->host << " -> " << (void *)host << " = {\n";
    if (host) {
        wdebug(1) << "    " << (int)host[0] << ", " << (int)host[1] << ", " << (int)host[2] << ", " << (int)host[3] << "...\n";
    }
    wdebug(1) << "  }\n";
    wdebug(1) << "  flags = " << buf->flags << "\n";
    wdebug(1) << "  type = " << (int)buf->type.code << "," << (int)buf->type.bits << "," << buf->type.lanes << "\n";
    wdebug(1) << "  dimensions = " << buf->dimensions << "\n";
    wdebug(1) << "  dim = " << buf->dim << " -> " << (void *)dim << " = {\n";
    for (int i = 0; i < buf->dimensions; i++) {
        const auto &d = dim[i];
        wdebug(1) << "    {" << d.min << "," << d.extent << "," << d.stride << "," << d.flags << "},\n";
    }
    wdebug(1) << "  }\n";
    wdebug(1) << "  padding = " << buf->padding << "\n";
    wdebug(1) << "}\n";
#endif
}

static_assert(sizeof(halide_type_t) == 4, "halide_type_t");
static_assert(sizeof(halide_dimension_t) == 16, "halide_dimension_t");
static_assert(sizeof(wasm_halide_buffer_t) == 40, "wasm_halide_buffer_t");

// Given a halide_buffer_t on the host, allocate a wasm_halide_buffer_t in wasm
// memory space and copy all relevant data. The resulting buf is laid out in
// contiguous memory, and can be free with a single free().
wasm32_ptr_t hostbuf_to_wasmbuf(WabtContext &wabt_context, const halide_buffer_t *src) {
    static_assert(sizeof(halide_type_t) == 4, "halide_type_t");
    static_assert(sizeof(halide_dimension_t) == 16, "halide_dimension_t");
    static_assert(sizeof(wasm_halide_buffer_t) == 40, "wasm_halide_buffer_t");

    wdebug(2) << "\nhostbuf_to_wasmbuf:\n";
    if (!src) {
        return 0;
    }

    dump_hostbuf(wabt_context, src, "src");

    wassert(src->device == 0);
    wassert(src->device_interface == nullptr);

    // Assume our malloc() has everything 32-byte aligned,
    // and insert enough padding for host to also be 32-byte aligned.
    const size_t dims_size_in_bytes = sizeof(halide_dimension_t) * src->dimensions;
    const size_t dims_offset = sizeof(wasm_halide_buffer_t);
    const size_t mem_needed_base = sizeof(wasm_halide_buffer_t) + dims_size_in_bytes;
    const size_t host_offset = align_up(mem_needed_base);
    const size_t host_size_in_bytes = src->size_in_bytes();
    const size_t mem_needed = host_offset + host_size_in_bytes;

    const wasm32_ptr_t dst_ptr = wabt_malloc(wabt_context, mem_needed);
    wassert(dst_ptr);

    uint8_t *base = get_wasm_memory_base(wabt_context);

    wasm_halide_buffer_t *dst = (wasm_halide_buffer_t *)(base + dst_ptr);
    dst->device = 0;
    dst->device_interface = 0;
    dst->host = src->host ? (dst_ptr + host_offset) : 0;
    dst->flags = src->flags;
    dst->type = src->type;
    dst->dimensions = src->dimensions;
    dst->dim = src->dimensions ? (dst_ptr + dims_offset) : 0;
    dst->padding = 0;

    if (src->dim) {
        memcpy(base + dst->dim, src->dim, dims_size_in_bytes);
    }
    if (src->host) {
        memcpy(base + dst->host, src->host, host_size_in_bytes);
    }

    dump_wasmbuf(wabt_context, dst_ptr, "dst");

    return dst_ptr;
}

// Given a pointer to a wasm_halide_buffer_t in wasm memory space,
// allocate a Buffer<> on the host and copy all relevant data.
void wasmbuf_to_hostbuf(WabtContext &wabt_context, wasm32_ptr_t src_ptr, Halide::Runtime::Buffer<> &dst) {
    wdebug(2) << "\nwasmbuf_to_hostbuf:\n";

    dump_wasmbuf(wabt_context, src_ptr, "src");

    wassert(src_ptr);

    uint8_t *base = get_wasm_memory_base(wabt_context);

    wasm_halide_buffer_t *src = (wasm_halide_buffer_t *)(base + src_ptr);

    wassert(src->device == 0);
    wassert(src->device_interface == 0);

    halide_buffer_t dst_tmp;
    dst_tmp.device = 0;
    dst_tmp.device_interface = 0;
    dst_tmp.host = nullptr;  // src->host ? (base + src->host) : nullptr;
    dst_tmp.flags = src->flags;
    dst_tmp.type = src->type;
    dst_tmp.dimensions = src->dimensions;
    dst_tmp.dim = src->dim ? (halide_dimension_t *)(base + src->dim) : nullptr;
    dst_tmp.padding = 0;

    dump_hostbuf(wabt_context, &dst_tmp, "dst_tmp");

    dst = Halide::Runtime::Buffer<>(dst_tmp);
    if (src->host) {
        // Don't use dst.copy(); it can tweak strides in ways that matter.
        dst.allocate();
        const size_t host_size_in_bytes = dst.raw_buffer()->size_in_bytes();
        memcpy(dst.raw_buffer()->host, base + src->host, host_size_in_bytes);
    }
    dump_hostbuf(wabt_context, dst.raw_buffer(), "dst");
}

// Given a wasm_halide_buffer_t, copy possibly-changed data into a halide_buffer_t.
// Both buffers are asserted to match in type and dimensions.
void copy_wasmbuf_to_existing_hostbuf(WabtContext &wabt_context, wasm32_ptr_t src_ptr, halide_buffer_t *dst) {
    wassert(src_ptr && dst);

    wdebug(2) << "\ncopy_wasmbuf_to_existing_hostbuf:\n";
    dump_wasmbuf(wabt_context, src_ptr, "src");

    uint8_t *base = get_wasm_memory_base(wabt_context);

    wasm_halide_buffer_t *src = (wasm_halide_buffer_t *)(base + src_ptr);
    wassert(src->device == 0);
    wassert(src->device_interface == 0);
    wassert(src->dimensions == dst->dimensions);
    wassert(src->type == dst->type);

    dump_hostbuf(wabt_context, dst, "dst_pre");

    if (src->dimensions) {
        memcpy(dst->dim, base + src->dim, sizeof(halide_dimension_t) * src->dimensions);
    }
    if (src->host) {
        size_t host_size_in_bytes = dst->size_in_bytes();
        memcpy(dst->host, base + src->host, host_size_in_bytes);
    }

    dst->device = 0;
    dst->device_interface = 0;
    dst->flags = src->flags;

    dump_hostbuf(wabt_context, dst, "dst_post");
}

// Given a halide_buffer_t, copy possibly-changed data into a wasm_halide_buffer_t.
// Both buffers are asserted to match in type and dimensions.
void copy_hostbuf_to_existing_wasmbuf(WabtContext &wabt_context, const halide_buffer_t *src, wasm32_ptr_t dst_ptr) {
    wassert(src && dst_ptr);

    wdebug(1) << "\ncopy_hostbuf_to_existing_wasmbuf:\n";
    dump_hostbuf(wabt_context, src, "src");

    uint8_t *base = get_wasm_memory_base(wabt_context);

    wasm_halide_buffer_t *dst = (wasm_halide_buffer_t *)(base + dst_ptr);
    wassert(src->device == 0);
    wassert(src->device_interface == 0);
    wassert(src->dimensions == dst->dimensions);
    wassert(src->type == dst->type);

    dump_wasmbuf(wabt_context, dst_ptr, "dst_pre");

    if (src->dimensions) {
        memcpy(base + dst->dim, src->dim, sizeof(halide_dimension_t) * src->dimensions);
    }
    if (src->host) {
        size_t host_size_in_bytes = src->size_in_bytes();
        memcpy(base + dst->host, src->host, host_size_in_bytes);
    }

    dst->device = 0;
    dst->device_interface = 0;
    dst->flags = src->flags;

    dump_wasmbuf(wabt_context, dst_ptr, "dst_post");
}

// --------------------------------------------------
// Helpers for converting to/from wabt::interp::Value
// --------------------------------------------------

template<typename T>
struct LoadValue {
    inline wabt::interp::Value operator()(const void *src) {
        const T val = *(const T *)(src);
        return wabt::interp::Value::Make(val);
    }
};

template<>
inline wabt::interp::Value LoadValue<bool>::operator()(const void *src) {
    // WABT doesn't do bools. Stash as u8 for now.
    const uint8_t val = *(const uint8_t *)src;
    return wabt::interp::Value::Make(val);
}

template<>
inline wabt::interp::Value LoadValue<void *>::operator()(const void *src) {
    // Halide 'handle' types are always uint64, even on 32-bit systems
    const uint64_t val = *(const uint64_t *)src;
    return wabt::interp::Value::Make(val);
}

template<>
inline wabt::interp::Value LoadValue<float16_t>::operator()(const void *src) {
    const uint16_t val = *(const uint16_t *)src;
    return wabt::interp::Value::Make(val);
}

template<>
inline wabt::interp::Value LoadValue<bfloat16_t>::operator()(const void *src) {
    const uint16_t val = *(const uint16_t *)src;
    return wabt::interp::Value::Make(val);
}

inline wabt::interp::Value load_value(const Type &t, const void *src) {
    return dynamic_type_dispatch<LoadValue>(t, src);
}

template<typename T>
inline wabt::interp::Value load_value(const T &val) {
    return LoadValue<T>()(&val);
}

// -----

template<typename T>
struct StoreValue {
    inline void operator()(const wabt::interp::Value &src, void *dst) {
        *(T *)dst = src.Get<T>();
    }
};

template<>
inline void StoreValue<bool>::operator()(const wabt::interp::Value &src, void *dst) {
    // WABT doesn't do bools. Stash as u8 for now.
    *(uint8_t *)dst = src.Get<uint8_t>();
}

template<>
inline void StoreValue<void *>::operator()(const wabt::interp::Value &src, void *dst) {
    // Halide 'handle' types are always uint64, even on 32-bit systems
    *(uint64_t *)dst = src.Get<uint64_t>();
}

template<>
inline void StoreValue<float16_t>::operator()(const wabt::interp::Value &src, void *dst) {
    *(uint16_t *)dst = src.Get<uint16_t>();
}

template<>
inline void StoreValue<bfloat16_t>::operator()(const wabt::interp::Value &src, void *dst) {
    *(uint16_t *)dst = src.Get<uint16_t>();
}

inline void store_value(const Type &t, const wabt::interp::Value &src, void *dst) {
    dynamic_type_dispatch<StoreValue>(t, src, dst);
}

template<typename T>
inline void store_value(const wabt::interp::Value &src, T *dst) {
    StoreValue<T>()(src, dst);
}

// --------------------------------------------------
// Host Callback Functions
// --------------------------------------------------

template<typename T, T some_func(T)>
wabt::Result wabt_posix_math_1(wabt::interp::Thread &thread,
                               const wabt::interp::Values &args,
                               wabt::interp::Values &results,
                               wabt::interp::Trap::Ptr *trap) {
    wassert(args.size() == 1);
    const T in = args[0].Get<T>();
    const T out = some_func(in);
    results[0] = wabt::interp::Value::Make(out);
    return wabt::Result::Ok;
}

template<typename T, T some_func(T, T)>
wabt::Result wabt_posix_math_2(wabt::interp::Thread &thread,
                               const wabt::interp::Values &args,
                               wabt::interp::Values &results,
                               wabt::interp::Trap::Ptr *trap) {
    wassert(args.size() == 2);
    const T in1 = args[0].Get<T>();
    const T in2 = args[1].Get<T>();
    const T out = some_func(in1, in2);
    results[0] = wabt::interp::Value::Make(out);
    return wabt::Result::Ok;
}

#define WABT_HOST_CALLBACK(x)                                              \
    wabt::Result wabt_jit_##x##_callback(wabt::interp::Thread &thread,     \
                                         const wabt::interp::Values &args, \
                                         wabt::interp::Values &results,    \
                                         wabt::interp::Trap::Ptr *trap)

#define WABT_HOST_CALLBACK_UNIMPLEMENTED(x)                                          \
    WABT_HOST_CALLBACK(x) {                                                          \
        internal_error << "WebAssembly JIT does not yet support the " #x "() call."; \
        return wabt::Result::Ok;                                                     \
    }

WABT_HOST_CALLBACK(__cxa_atexit) {
    // nothing
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK(__extendhfsf2) {
    const uint16_t in = args[0].Get<uint16_t>();
    const float out = (float)float16_t::make_from_bits(in);
    results[0] = wabt::interp::Value::Make(out);
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK(__truncsfhf2) {
    const float in = args[0].Get<float>();
    const uint16_t out = float16_t(in).to_bits();
    results[0] = wabt::interp::Value::Make(out);
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK(abort) {
    abort();
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK_UNIMPLEMENTED(fclose)

WABT_HOST_CALLBACK_UNIMPLEMENTED(fileno)

WABT_HOST_CALLBACK_UNIMPLEMENTED(fopen)

WABT_HOST_CALLBACK(free) {
    WabtContext &wabt_context = get_wabt_context(thread);

    wasm32_ptr_t p = args[0].Get<int32_t>();
    if (p) p -= kExtraMallocSlop;
    wabt_free(wabt_context, p);
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK_UNIMPLEMENTED(fwrite)

WABT_HOST_CALLBACK(getenv) {
    WabtContext &wabt_context = get_wabt_context(thread);

    const int32_t s = args[0].Get<int32_t>();

    uint8_t *base = get_wasm_memory_base(wabt_context);
    char *e = getenv((char *)base + s);

    // TODO: this string is leaked
    if (e) {
        wasm32_ptr_t r = wabt_malloc(wabt_context, strlen(e) + 1);
        strcpy((char *)base + r, e);
        results[0] = wabt::interp::Value::Make(r);
    } else {
        results[0] = wabt::interp::Value::Make(0);
    }
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK(halide_print) {
    WabtContext &wabt_context = get_wabt_context(thread);

    wassert(args.size() == 2);

    JITUserContext *jit_user_context = get_jit_user_context(wabt_context, args[0]);
    const int32_t str_address = args[1].Get<int32_t>();

    uint8_t *p = get_wasm_memory_base(wabt_context);
    const char *str = (const char *)p + str_address;

    if (jit_user_context && jit_user_context->handlers.custom_print != NULL) {
        (*jit_user_context->handlers.custom_print)(jit_user_context, str);
    } else {
        std::cout << str;
    }
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK(halide_trace_helper) {
    WabtContext &wabt_context = get_wabt_context(thread);

    wassert(args.size() == 12);

    uint8_t *base = get_wasm_memory_base(wabt_context);

    JITUserContext *jit_user_context = get_jit_user_context(wabt_context, args[0]);

    const wasm32_ptr_t func_name_ptr = args[1].Get<int32_t>();
    const wasm32_ptr_t value_ptr = args[2].Get<int32_t>();
    const wasm32_ptr_t coordinates_ptr = args[3].Get<int32_t>();
    const int type_code = args[4].Get<int32_t>();
    const int type_bits = args[5].Get<int32_t>();
    const int type_lanes = args[6].Get<int32_t>();
    const int trace_code = args[7].Get<int32_t>();
    const int parent_id = args[8].Get<int32_t>();
    const int value_index = args[9].Get<int32_t>();
    const int dimensions = args[10].Get<int32_t>();
    const wasm32_ptr_t trace_tag_ptr = args[11].Get<int32_t>();

    wassert(dimensions >= 0 && dimensions < 1024);  // not a hard limit, just a sanity check

    halide_trace_event_t event;
    event.func = (const char *)(base + func_name_ptr);
    event.value = value_ptr ? ((void *)(base + value_ptr)) : nullptr;
    event.coordinates = coordinates_ptr ? ((int32_t *)(base + coordinates_ptr)) : nullptr;
    event.trace_tag = (const char *)(base + trace_tag_ptr);
    event.type.code = (halide_type_code_t)type_code;
    event.type.bits = (uint8_t)type_bits;
    event.type.lanes = (uint16_t)type_lanes;
    event.event = (halide_trace_event_code_t)trace_code;
    event.parent_id = parent_id;
    event.value_index = value_index;
    event.dimensions = dimensions;

    int32_t result = 0;
    if (jit_user_context && jit_user_context->handlers.custom_trace != NULL) {
        result = (*jit_user_context->handlers.custom_trace)(jit_user_context, &event);
    } else {
        debug(0) << "Dropping trace event due to lack of trace handler.\n";
    }

    results[0] = wabt::interp::Value::Make(result);
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK(halide_error) {
    WabtContext &wabt_context = get_wabt_context(thread);

    wassert(args.size() == 2);

    JITUserContext *jit_user_context = get_jit_user_context(wabt_context, args[0]);
    const int32_t str_address = args[1].Get<int32_t>();

    uint8_t *p = get_wasm_memory_base(wabt_context);
    const char *str = (const char *)p + str_address;

    if (jit_user_context && jit_user_context->handlers.custom_error != NULL) {
        (*jit_user_context->handlers.custom_error)(jit_user_context, str);
    } else {
        halide_runtime_error << str;
    }
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK(malloc) {
    WabtContext &wabt_context = get_wabt_context(thread);

    size_t size = args[0].Get<int32_t>() + kExtraMallocSlop;
    wasm32_ptr_t p = wabt_malloc(wabt_context, size);
    if (p) p += kExtraMallocSlop;
    results[0] = wabt::interp::Value::Make(p);
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK(memcpy) {
    WabtContext &wabt_context = get_wabt_context(thread);

    const int32_t dst = args[0].Get<int32_t>();
    const int32_t src = args[1].Get<int32_t>();
    const int32_t n = args[2].Get<int32_t>();

    uint8_t *base = get_wasm_memory_base(wabt_context);

    memcpy(base + dst, base + src, n);

    results[0] = wabt::interp::Value::Make(dst);
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK(memset) {
    WabtContext &wabt_context = get_wabt_context(thread);

    const int32_t s = args[0].Get<int32_t>();
    const int32_t c = args[1].Get<int32_t>();
    const int32_t n = args[2].Get<int32_t>();

    uint8_t *base = get_wasm_memory_base(wabt_context);
    memset(base + s, c, n);

    results[0] = wabt::interp::Value::Make(s);
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK(memcmp) {
    WabtContext &wabt_context = get_wabt_context(thread);

    const int32_t s1 = args[0].Get<int32_t>();
    const int32_t s2 = args[1].Get<int32_t>();
    const int32_t n = args[2].Get<int32_t>();

    uint8_t *base = get_wasm_memory_base(wabt_context);

    const int32_t r = memcmp(base + s1, base + s2, n);

    results[0] = wabt::interp::Value::Make(r);
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK(strlen) {
    WabtContext &wabt_context = get_wabt_context(thread);
    const int32_t s = args[0].Get<int32_t>();

    uint8_t *base = get_wasm_memory_base(wabt_context);
    int32_t r = strlen((char *)base + s);

    results[0] = wabt::interp::Value::Make(r);
    return wabt::Result::Ok;
}

WABT_HOST_CALLBACK_UNIMPLEMENTED(write)

using HostCallbackMap = std::unordered_map<std::string, wabt::interp::HostFunc::Callback>;

// clang-format off
const HostCallbackMap &get_host_callback_map() {

    static HostCallbackMap m = {
        // General runtime functions.

        #define DEFINE_CALLBACK(f) { #f, wabt_jit_##f##_callback },

        DEFINE_CALLBACK(__cxa_atexit)
        DEFINE_CALLBACK(__extendhfsf2)
        DEFINE_CALLBACK(__truncsfhf2)
        DEFINE_CALLBACK(abort)
        DEFINE_CALLBACK(fclose)
        DEFINE_CALLBACK(fileno)
        DEFINE_CALLBACK(fopen)
        DEFINE_CALLBACK(free)
        DEFINE_CALLBACK(fwrite)
        DEFINE_CALLBACK(getenv)
        DEFINE_CALLBACK(halide_error)
        DEFINE_CALLBACK(halide_print)
        DEFINE_CALLBACK(halide_trace_helper)
        DEFINE_CALLBACK(malloc)
        DEFINE_CALLBACK(memcmp)
        DEFINE_CALLBACK(memcpy)
        DEFINE_CALLBACK(memset)
        DEFINE_CALLBACK(strlen)
        DEFINE_CALLBACK(write)

        #undef DEFINE_CALLBACK

        // Posix math.
        #define DEFINE_POSIX_MATH_CALLBACK(t, f) { #f, wabt_posix_math_1<t, ::f> },

        DEFINE_POSIX_MATH_CALLBACK(double, acos)
        DEFINE_POSIX_MATH_CALLBACK(double, acosh)
        DEFINE_POSIX_MATH_CALLBACK(double, asin)
        DEFINE_POSIX_MATH_CALLBACK(double, asinh)
        DEFINE_POSIX_MATH_CALLBACK(double, atan)
        DEFINE_POSIX_MATH_CALLBACK(double, atanh)
        DEFINE_POSIX_MATH_CALLBACK(double, cos)
        DEFINE_POSIX_MATH_CALLBACK(double, cosh)
        DEFINE_POSIX_MATH_CALLBACK(double, exp)
        DEFINE_POSIX_MATH_CALLBACK(double, log)
        DEFINE_POSIX_MATH_CALLBACK(double, round)
        DEFINE_POSIX_MATH_CALLBACK(double, sin)
        DEFINE_POSIX_MATH_CALLBACK(double, sinh)
        DEFINE_POSIX_MATH_CALLBACK(double, tan)
        DEFINE_POSIX_MATH_CALLBACK(double, tanh)

        DEFINE_POSIX_MATH_CALLBACK(float, acosf)
        DEFINE_POSIX_MATH_CALLBACK(float, acoshf)
        DEFINE_POSIX_MATH_CALLBACK(float, asinf)
        DEFINE_POSIX_MATH_CALLBACK(float, asinhf)
        DEFINE_POSIX_MATH_CALLBACK(float, atanf)
        DEFINE_POSIX_MATH_CALLBACK(float, atanhf)
        DEFINE_POSIX_MATH_CALLBACK(float, cosf)
        DEFINE_POSIX_MATH_CALLBACK(float, coshf)
        DEFINE_POSIX_MATH_CALLBACK(float, expf)
        DEFINE_POSIX_MATH_CALLBACK(float, logf)
        DEFINE_POSIX_MATH_CALLBACK(float, roundf)
        DEFINE_POSIX_MATH_CALLBACK(float, sinf)
        DEFINE_POSIX_MATH_CALLBACK(float, sinhf)
        DEFINE_POSIX_MATH_CALLBACK(float, tanf)
        DEFINE_POSIX_MATH_CALLBACK(float, tanhf)

        #undef DEFINE_POSIX_MATH_CALLBACK

        #define DEFINE_POSIX_MATH_CALLBACK2(t, f) { #f, wabt_posix_math_2<t, ::f> },

        DEFINE_POSIX_MATH_CALLBACK2(float, atan2f)
        DEFINE_POSIX_MATH_CALLBACK2(double, atan2)
        DEFINE_POSIX_MATH_CALLBACK2(float, fminf)
        DEFINE_POSIX_MATH_CALLBACK2(double, fmin)
        DEFINE_POSIX_MATH_CALLBACK2(float, fmaxf)
        DEFINE_POSIX_MATH_CALLBACK2(double, fmax)
        DEFINE_POSIX_MATH_CALLBACK2(float, powf)
        DEFINE_POSIX_MATH_CALLBACK2(double, pow)

        #undef DEFINE_POSIX_MATH_CALLBACK2
    };

    return m;
}
// clang-format on

// --------------------------------------------------
// Host Callback Functions
// --------------------------------------------------

struct ExternArgType {
    halide_type_t type;
    bool is_void;
    bool is_buffer;
};

using TrampolineFn = void (*)(void **);

wabt::Result extern_callback_wrapper(const std::vector<ExternArgType> &arg_types,
                                     TrampolineFn trampoline_fn,
                                     wabt::interp::Thread &thread,
                                     const wabt::interp::Values &args,
                                     wabt::interp::Values &results,
                                     wabt::interp::Trap::Ptr *trap) {
    WabtContext &wabt_context = get_wabt_context(thread);

    wassert(arg_types.size() >= 1);
    const size_t arg_types_len = arg_types.size() - 1;
    const ExternArgType &ret_type = arg_types[0];

    // There's wasted space here, but that's ok.
    std::vector<Halide::Runtime::Buffer<>> buffers(arg_types_len);
    std::vector<uint64_t> scalars(arg_types_len, 0);
    std::vector<void *> trampoline_args(arg_types_len, nullptr);

    for (size_t i = 0; i < arg_types_len; ++i) {
        const auto &a = arg_types[i + 1];
        if (a.is_buffer) {
            const wasm32_ptr_t buf_ptr = args[i].Get<int32_t>();
            wasmbuf_to_hostbuf(wabt_context, buf_ptr, buffers[i]);
            trampoline_args[i] = buffers[i].raw_buffer();
        } else {
            store_value(a.type, args[i], &scalars[i]);
            trampoline_args[i] = &scalars[i];
        }
    }

    // The return value (if any) is always scalar.
    uint64_t ret_val = 0;
    const bool has_retval = !ret_type.is_void;
    internal_assert(!ret_type.is_buffer);
    if (has_retval) {
        trampoline_args.push_back(&ret_val);
    }
    (*trampoline_fn)(trampoline_args.data());

    if (has_retval) {
        results[0] = dynamic_type_dispatch<LoadValue>(ret_type.type, (void *)&ret_val);
    }

    // Progagate buffer data backwards. Note that for arbitrary extern functions,
    // we have no idea which buffers might be "input only", so we copy all data for all of them.
    for (size_t i = 0; i < arg_types_len; ++i) {
        const auto &a = arg_types[i + 1];
        if (a.is_buffer) {
            const wasm32_ptr_t buf_ptr = args[i].Get<int32_t>();
            copy_hostbuf_to_existing_wasmbuf(wabt_context, buffers[i], buf_ptr);
        }
    }

    return wabt::Result::Ok;
}

bool should_skip_extern_symbol(const std::string &name) {
    static std::set<std::string> symbols = {
        "halide_print",
        "halide_error"};
    return symbols.count(name) > 0;
}

wabt::interp::HostFunc::Ptr make_extern_callback(wabt::interp::Store &store,
                                                 const std::map<std::string, Halide::JITExtern> &jit_externs,
                                                 const JITModule &trampolines,
                                                 const wabt::interp::ImportDesc &import) {
    const std::string &fn_name = import.type.name;
    if (should_skip_extern_symbol(fn_name)) {
        wdebug(1) << "Skipping extern symbol: " << fn_name << "\n";
        return wabt::interp::HostFunc::Ptr();
    }

    const auto it = jit_externs.find(fn_name);
    if (it == jit_externs.end()) {
        wdebug(1) << "Extern symbol not found in JIT Externs: " << fn_name << "\n";
        return wabt::interp::HostFunc::Ptr();
    }
    const ExternSignature &sig = it->second.extern_c_function().signature();

    const auto &tramp_it = trampolines.exports().find(fn_name + kTrampolineSuffix);
    if (tramp_it == trampolines.exports().end()) {
        wdebug(1) << "Extern symbol not found in trampolines: " << fn_name << "\n";
        return wabt::interp::HostFunc::Ptr();
    }
    TrampolineFn trampoline_fn = (TrampolineFn)tramp_it->second.address;

    const size_t arg_count = sig.arg_types().size();

    std::vector<ExternArgType> arg_types;

    if (sig.is_void_return()) {
        const bool is_void = true;
        const bool is_buffer = false;
        // Specifying a type here with bits == 0 should trigger a proper 'void' return type
        arg_types.push_back(ExternArgType{{halide_type_int, 0, 0}, is_void, is_buffer});
    } else {
        const Type &t = sig.ret_type();
        const bool is_void = false;
        const bool is_buffer = (t == type_of<halide_buffer_t *>());
        user_assert(t.lanes() == 1) << "Halide Extern functions cannot return vector values.";
        user_assert(!is_buffer) << "Halide Extern functions cannot return halide_buffer_t.";
        arg_types.push_back(ExternArgType{t, is_void, is_buffer});
    }
    for (size_t i = 0; i < arg_count; ++i) {
        const Type &t = sig.arg_types()[i];
        const bool is_void = false;
        const bool is_buffer = (t == type_of<halide_buffer_t *>());
        user_assert(t.lanes() == 1) << "Halide Extern functions cannot accept vector values as arguments.";
        arg_types.push_back(ExternArgType{t, is_void, is_buffer});
    }

    const auto callback_wrapper =
        [arg_types, trampoline_fn](wabt::interp::Thread &thread,
                                   const wabt::interp::Values &args,
                                   wabt::interp::Values &results,
                                   wabt::interp::Trap::Ptr *trap) -> wabt::Result {
        return extern_callback_wrapper(arg_types, trampoline_fn, thread, args, results, trap);
    };

    auto func_type = *wabt::cast<wabt::interp::FuncType>(import.type.type.get());
    auto host_func = wabt::interp::HostFunc::New(store, func_type, callback_wrapper);

    return host_func;
}

wabt::Features calc_features(const Target &target) {
    wabt::Features f;
    if (target.has_feature(Target::WasmSignExt)) {
        f.enable_sign_extension();
    }
    if (target.has_feature(Target::WasmSimd128)) {
        f.enable_simd();
    }
    if (target.has_feature(Target::WasmSatFloatToInt)) {
        f.enable_sat_float_to_int();
    }
    return f;
}

}  // namespace

#endif  // WITH_WABT

struct WasmModuleContents {
    mutable RefCount ref_count;

    const Target target;
    const std::vector<Argument> arguments;
    std::map<std::string, Halide::JITExtern> jit_externs;
    std::vector<JITModule> extern_deps;
    JITModule trampolines;

#if WITH_WABT
    BDMalloc bdmalloc;
    wabt::interp::Store store;
    wabt::interp::Module::Ptr module;
    wabt::interp::Instance::Ptr instance;
    wabt::interp::Thread::Options thread_options;
    wabt::interp::Memory::Ptr memory;
#endif

    WasmModuleContents(
        const Module &halide_module,
        const std::vector<Argument> &arguments,
        const std::string &fn_name,
        const std::map<std::string, Halide::JITExtern> &jit_externs,
        const std::vector<JITModule> &extern_deps);

    int run(const void **args);

    ~WasmModuleContents();
};

WasmModuleContents::WasmModuleContents(
    const Module &halide_module,
    const std::vector<Argument> &arguments,
    const std::string &fn_name,
    const std::map<std::string, Halide::JITExtern> &jit_externs,
    const std::vector<JITModule> &extern_deps)
    : target(halide_module.target()),
      arguments(arguments),
      jit_externs(jit_externs),
      extern_deps(extern_deps),
      trampolines(JITModule::make_trampolines_module(get_host_target(), jit_externs, kTrampolineSuffix, extern_deps)) {

#if WITH_WABT
    user_assert(LLVM_VERSION >= 110) << "Using the WebAssembly JIT is only supported under LLVM 11+.";

    wdebug(1) << "Compiling wasm function " << fn_name << "\n";

    // Compile halide into wasm bytecode.
    std::vector<char> final_wasm = compile_to_wasm(halide_module, fn_name);

    store = wabt::interp::Store(calc_features(halide_module.target()));

    // Create a wabt Module for it.
    wabt::MemoryStream log_stream;
    constexpr bool kReadDebugNames = true;
    constexpr bool kStopOnFirstError = true;
    constexpr bool kFailOnCustomSectionError = true;
    wabt::ReadBinaryOptions options(store.features(),
                                    &log_stream,
                                    kReadDebugNames,
                                    kStopOnFirstError,
                                    kFailOnCustomSectionError);
    wabt::Errors errors;
    wabt::interp::ModuleDesc module_desc;
    wabt::Result r = wabt::interp::ReadBinaryInterp(final_wasm.data(),
                                                    final_wasm.size(),
                                                    options,
                                                    &errors,
                                                    &module_desc);
    internal_assert(Succeeded(r))
        << "ReadBinaryInterp failed:\n"
        << wabt::FormatErrorsToString(errors, wabt::Location::Type::Binary) << "\n"
        << "  log: " << to_string(log_stream) << "\n";

    if (WASM_DEBUG_LEVEL >= 2) {
        wabt::MemoryStream dis_stream;
        module_desc.istream.Disassemble(&dis_stream);
        wdebug(WASM_DEBUG_LEVEL) << "Disassembly:\n"
                                 << to_string(dis_stream) << "\n";
    }

    module = wabt::interp::Module::New(store, module_desc);

    // Bind all imports to our callbacks.
    wabt::interp::RefVec imports;
    const HostCallbackMap &host_callback_map = get_host_callback_map();
    for (const auto &import : module->desc().imports) {
        wdebug(1) << "import=" << import.type.module << "." << import.type.name << "\n";
        if (import.type.type->kind == wabt::interp::ExternKind::Func && import.type.module == "env") {
            auto it = host_callback_map.find(import.type.name);
            if (it != host_callback_map.end()) {
                auto func_type = *wabt::cast<wabt::interp::FuncType>(import.type.type.get());
                auto host_func = wabt::interp::HostFunc::New(store, func_type, it->second);
                imports.push_back(host_func.ref());
                continue;
            }

            // If it's not one of the standard host callbacks, assume it must be
            // a define_extern, and look for it in the jit_externs.
            auto host_func = make_extern_callback(store, jit_externs, trampolines, import);
            imports.push_back(host_func.ref());
            continue;
        }
        // By default, just push a null reference. This won't resolve, and
        // instantiation will fail.
        imports.push_back(wabt::interp::Ref::Null);
    }

    wabt::interp::RefPtr<wabt::interp::Trap> trap;
    instance = wabt::interp::Instance::Instantiate(store, module.ref(), imports, &trap);
    internal_assert(instance) << "Error initializing module: " << trap->message() << "\n";

    int32_t heap_base = -1;

    for (const auto &e : module_desc.exports) {
        if (e.type.name == "__heap_base") {
            internal_assert(e.type.type->kind == wabt::ExternalKind::Global);
            heap_base = store.UnsafeGet<wabt::interp::Global>(instance->globals()[e.index])->Get().Get<int32_t>();
            wdebug(1) << "__heap_base is " << heap_base << "\n";
            continue;
        }
        if (e.type.name == "memory") {
            internal_assert(e.type.type->kind == wabt::ExternalKind::Memory);
            internal_assert(!memory.get()) << "Expected exactly one memory object but saw " << (void *)memory.get();
            memory = store.UnsafeGet<wabt::interp::Memory>(instance->memories()[e.index]);
            wdebug(1) << "heap_size is " << memory->ByteSize() << "\n";
            continue;
        }
    }
    internal_assert(heap_base >= 0) << "__heap_base not found";
    internal_assert(memory->ByteSize() > 0) << "memory size is unlikely";

    bdmalloc.init(memory->ByteSize(), heap_base);

#endif  // WITH_WABT
}

int WasmModuleContents::run(const void **args) {
#if WITH_WABT
    const auto &module_desc = module->desc();

    wabt::interp::FuncType *func_type = nullptr;
    wabt::interp::RefPtr<wabt::interp::Func> func;
    std::string func_name;

    for (const auto &e : module_desc.exports) {
        if (e.type.type->kind == wabt::ExternalKind::Func) {
            wdebug(1) << "Selecting export '" << e.type.name << "'\n";
            internal_assert(!func_type && !func) << "Multiple exported funcs found";
            func_type = wabt::cast<wabt::interp::FuncType>(e.type.type.get());
            func = store.UnsafeGet<wabt::interp::Func>(instance->funcs()[e.index]);
            func_name = e.type.name;
            continue;
        }
    }

    JITUserContext *jit_user_context = nullptr;
    for (size_t i = 0; i < arguments.size(); i++) {
        const Argument &arg = arguments[i];
        const void *arg_ptr = args[i];
        if (arg.name == "__user_context") {
            jit_user_context = *(JITUserContext **)const_cast<void *>(arg_ptr);
        }
    }

    WabtContext wabt_context(jit_user_context, *memory, bdmalloc);

    wabt::interp::Values wabt_args;
    wabt::interp::Values wabt_results;
    wabt::interp::Trap::Ptr trap;

    std::vector<wasm32_ptr_t> wbufs(arguments.size(), 0);

    for (size_t i = 0; i < arguments.size(); i++) {
        const Argument &arg = arguments[i];
        const void *arg_ptr = args[i];
        if (arg.is_buffer()) {
            halide_buffer_t *buf = (halide_buffer_t *)const_cast<void *>(arg_ptr);
            // It's OK for this to be null (let Halide asserts handle it)
            wasm32_ptr_t wbuf = hostbuf_to_wasmbuf(wabt_context, buf);
            wbufs[i] = wbuf;
            wabt_args.push_back(load_value(wbuf));
        } else {
            if (arg.name == "__user_context") {
                wabt_args.push_back(wabt::interp::Value::Make(kMagicJitUserContextValue));
            } else {
                wabt_args.push_back(load_value(arg.type, arg_ptr));
            }
        }
    }

    wabt::interp::Thread::Options options;
    wabt::interp::Thread::Ptr thread = wabt::interp::Thread::New(store, options);
    thread->set_host_info(&wabt_context);

    auto r = func->Call(*thread, wabt_args, wabt_results, &trap);
    if (WASM_DEBUG_LEVEL >= 2) {
        wabt::MemoryStream call_stream;
        WriteCall(&call_stream, func_name, *func_type, wabt_args, wabt_results, trap);
        wdebug(WASM_DEBUG_LEVEL) << to_string(call_stream) << "\n";
    }
    internal_assert(Succeeded(r)) << "Func::Call failed: " << trap->message() << "\n";
    internal_assert(wabt_results.size() == 1);
    int32_t result = wabt_results[0].Get<int32_t>();

    wdebug(1) << "Result is " << result << "\n";

    if (result == 0) {
        // Update any output buffers
        for (size_t i = 0; i < arguments.size(); i++) {
            const Argument &arg = arguments[i];
            const void *arg_ptr = args[i];
            if (arg.is_buffer()) {
                halide_buffer_t *buf = (halide_buffer_t *)const_cast<void *>(arg_ptr);
                copy_wasmbuf_to_existing_hostbuf(wabt_context, wbufs[i], buf);
            }
        }
    }

    for (wasm32_ptr_t p : wbufs) {
        wabt_free(wabt_context, p);
    }

    // Don't do this: things allocated by Halide runtime might need to persist
    // between multiple invocations of the same function.
    // bdmalloc.reset();

    return result;

#endif

    internal_error << "WasmExecutor is not configured correctly";
    return -1;
}

WasmModuleContents::~WasmModuleContents() {
#if WITH_WABT
    // nothing
#endif
}

template<>
RefCount &ref_count<WasmModuleContents>(const WasmModuleContents *p) noexcept {
    return p->ref_count;
}

template<>
void destroy<WasmModuleContents>(const WasmModuleContents *p) {
    delete p;
}

/*static*/
bool WasmModule::can_jit_target(const Target &target) {
#if WITH_WABT
    if (target.arch == Target::WebAssembly) {
        return true;
    }
#endif
    return false;
}

/*static*/
WasmModule WasmModule::compile(
    const Module &module,
    const std::vector<Argument> &arguments,
    const std::string &fn_name,
    const std::map<std::string, Halide::JITExtern> &jit_externs,
    const std::vector<JITModule> &extern_deps) {
#if !defined(WITH_WABT)
    user_error << "Cannot run JITted WebAssembly without configuring a WebAssembly engine.";
    return WasmModule();
#endif

    WasmModule wasm_module;
    wasm_module.contents = new WasmModuleContents(module, arguments, fn_name, jit_externs, extern_deps);
    return wasm_module;
}

/** Run generated previously compiled wasm code with a set of arguments. */
int WasmModule::run(const void **args) {
    internal_assert(contents.defined());
    return contents->run(args);
}

}  // namespace Internal
}  // namespace Halide
