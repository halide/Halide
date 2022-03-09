#include "WasmExecutor.h"

#include <cmath>
#include <csignal>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "CodeGen_Posix.h"
#include "CodeGen_Targets.h"
#include "Error.h"
#include "Float16.h"
#include "Func.h"
#include "ImageParam.h"
#include "JITModule.h"
#if WITH_WABT || WITH_V8
#include "LLVM_Headers.h"
#endif
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"
#include "Target.h"

#if WITH_WABT
#include "wabt-src/src/binary-reader.h"
#include "wabt-src/src/cast.h"
#include "wabt-src/src/common.h"
#include "wabt-src/src/error-formatter.h"
#include "wabt-src/src/error.h"
#include "wabt-src/src/feature.h"
#include "wabt-src/src/interp/binary-reader-interp.h"
#include "wabt-src/src/interp/interp-util.h"
#include "wabt-src/src/interp/interp.h"
#include "wabt-src/src/interp/istream.h"
#include "wabt-src/src/result.h"
#include "wabt-src/src/stream.h"
#endif

// clang-format off
// These includes are order-dependent, don't let clang-format reorder them
#ifdef WITH_V8
#include "v8.h"
#include "libplatform/libplatform.h"
#endif  // WITH_V8
// clang-format on

namespace Halide {
namespace Internal {

// Trampolines do not use "_argv" as the suffix because
// that name may already exist and if so, will return an int
// instead of taking a pointer at the end of the args list to
// receive the result value.
static const char kTrampolineSuffix[] = "_trampoline";

#ifdef WITH_V8
using namespace v8;

#define V8_API_VERSION ((V8_MAJOR_VERSION * 10) + V8_MINOR_VERSION)

static_assert(V8_API_VERSION >= 98,
              "Halide requires V8 v9.8 or later when compiling WITH_V8.");
#endif

#if WITH_WABT || WITH_V8

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

        for (auto &region : regions) {
            const uint32_t start = region.first;
            Region &r = region.second;
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
        std::unique_ptr<CodeGen_Posix> cg(new_CodeGen_WebAssembly(module.target()));
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

#if LLVM_VERSION >= 140
    if (!lld::wasm::link(lld_args, llvm::outs(), llvm::errs(), /*canExitEarly*/ false, /*disableOutput*/ false)) {
        std::signal(SIGABRT, old_abort_handler);
        internal_error << "lld::wasm::link failed\n";
    }
#else
    if (!lld::wasm::link(lld_args, /*CanExitEarly*/ false, llvm::outs(), llvm::errs())) {
        std::signal(SIGABRT, old_abort_handler);
        internal_error << "lld::wasm::link failed\n";
    }
#endif

    std::signal(SIGABRT, old_abort_handler);

#if WASM_DEBUG_LEVEL
    wasm_output.detach();
    wdebug(1) << "Dumping linked wasm to " << wasm_output.pathname() << "\n";
#endif

    return read_entire_file(wasm_output.pathname());
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
    case halide_type_t(CODE, BITS).as_u32(): \
        return Functor<TYPE>()(std::forward<Args>(args)...);

    switch (type.element_of().as_u32()) {
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

// -----------------------
// extern callback helper code
// -----------------------

struct ExternArgType {
    halide_type_t type;
    bool is_void;
    bool is_buffer;
    bool is_ucon;
};

using TrampolineFn = void (*)(void **);

bool build_extern_arg_types(const std::string &fn_name,
                            const std::map<std::string, Halide::JITExtern> &jit_externs,
                            const JITModule &trampolines,
                            TrampolineFn &trampoline_fn_out,
                            std::vector<ExternArgType> &arg_types_out) {
    const auto it = jit_externs.find(fn_name);
    if (it == jit_externs.end()) {
        wdebug(1) << "Extern symbol not found in JIT Externs: " << fn_name << "\n";
        return false;
    }
    const ExternSignature &sig = it->second.extern_c_function().signature();

    const auto &tramp_it = trampolines.exports().find(fn_name + kTrampolineSuffix);
    if (tramp_it == trampolines.exports().end()) {
        wdebug(1) << "Extern symbol not found in trampolines: " << fn_name << "\n";
        return false;
    }

    trampoline_fn_out = (TrampolineFn)tramp_it->second.address;

    const size_t arg_count = sig.arg_types().size();
    std::vector<ExternArgType> arg_types;
    arg_types.reserve(arg_count + 1);

    if (sig.is_void_return()) {
        const bool is_void = true;
        const bool is_buffer = false;
        const bool is_ucon = false;
        // Specifying a type here with bits == 0 should trigger a proper 'void' return type
        arg_types.emplace_back(ExternArgType{{halide_type_int, 0, 0}, is_void, is_buffer, is_ucon});
    } else {
        const Type &t = sig.ret_type();
        const bool is_void = false;
        const bool is_buffer = (t == type_of<halide_buffer_t *>());
        const bool is_ucon = false;
        user_assert(t.lanes() == 1) << "Halide Extern functions cannot return vector values.";
        user_assert(!is_buffer) << "Halide Extern functions cannot return halide_buffer_t.";

        // TODO: the assertion below could be removed if we are ever able to marshal int64
        // values across the barrier, but that may require wasm codegen changes that are tricky.
        user_assert(!(t.is_handle() && !is_buffer)) << "Halide Extern functions cannot return arbitrary pointers as arguments.";
        user_assert(!(t.is_int_or_uint() && t.bits() == 64)) << "Halide Extern functions cannot accept 64-bit values as arguments.";

        arg_types.emplace_back(ExternArgType{t, is_void, is_buffer, is_ucon});
    }

    for (size_t i = 0; i < arg_count; ++i) {
        const Type &t = sig.arg_types()[i];
        const bool is_void = false;
        const bool is_buffer = (t == type_of<halide_buffer_t *>());
        // Since arbitrary pointer args aren't legal for extern calls,
        // assume that anything that is a void* is a user context.
        const bool is_ucon = (t == type_of<void *>());
        user_assert(t.lanes() == 1) << "Halide Extern functions cannot accept vector values as arguments.";

        // TODO: the assertion below could be removed if we are ever able to marshal int64
        // values across the barrier, but that may require wasm codegen changes that are tricky.
        user_assert(!(t.is_handle() && !is_buffer)) << "Halide Extern functions cannot accept arbitrary pointers as arguments.";
        user_assert(!(t.is_int_or_uint() && t.bits() == 64)) << "Halide Extern functions cannot accept 64-bit values as arguments.";

        arg_types.emplace_back(ExternArgType{t, is_void, is_buffer, is_ucon});
    }

    arg_types_out = std::move(arg_types);
    return true;
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

static_assert(sizeof(halide_type_t) == 4, "halide_type_t");
static_assert(sizeof(halide_dimension_t) == 16, "halide_dimension_t");
static_assert(sizeof(wasm_halide_buffer_t) == 40, "wasm_halide_buffer_t");

#if WITH_WABT
std::string to_string(const wabt::MemoryStream &m) {
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
    void *host_info = thread.GetCallerInstance()->host_info();
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

void dump_hostbuf(WabtContext &wabt_context, const halide_buffer_t *buf, const std::string &label) {
#if WASM_DEBUG_LEVEL >= 2
    const halide_dimension_t *dim = buf->dim;
    const uint8_t *host = buf->host;

    wdebug(1) << label << " = " << (const void *)buf << " = {\n";
    wdebug(1) << "  device = " << buf->device << "\n";
    wdebug(1) << "  device_interface = " << buf->device_interface << "\n";
    wdebug(1) << "  host = " << (const void *)host << " = {\n";
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
    dst_tmp.device_interface = nullptr;
    dst_tmp.host = nullptr;  // src->host ? (base + src->host) : nullptr;
    dst_tmp.flags = src->flags;
    dst_tmp.type = src->type;
    dst_tmp.dimensions = src->dimensions;
    dst_tmp.dim = src->dim ? (halide_dimension_t *)(base + src->dim) : nullptr;
    dst_tmp.padding = nullptr;

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
    dst->device_interface = nullptr;
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
    if (p) {
        p -= kExtraMallocSlop;
    }
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

    if (jit_user_context && jit_user_context->handlers.custom_print != nullptr) {
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
    if (jit_user_context && jit_user_context->handlers.custom_trace != nullptr) {
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

    if (jit_user_context && jit_user_context->handlers.custom_error != nullptr) {
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
    if (p) {
        p += kExtraMallocSlop;
    }
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

WABT_HOST_CALLBACK(memmove) {
    WabtContext &wabt_context = get_wabt_context(thread);

    const int32_t dst = args[0].Get<int32_t>();
    const int32_t src = args[1].Get<int32_t>();
    const int32_t n = args[2].Get<int32_t>();

    uint8_t *base = get_wasm_memory_base(wabt_context);

    memmove(base + dst, base + src, n);

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

// --------------------------------------------------
// Host Callback Functions
// --------------------------------------------------

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
        if (a.is_ucon) {
            // We have to special-case ucon because Halide considers it an int64 everywhere
            // (even for wasm, where pointers are int32), and trying to extract it as an
            // int64 from a Value that is int32 will assert-fail. In JIT mode the value
            // doesn't even matter (except for guarding that it is our predicted constant).
            wassert(args[i].Get<int32_t>() == 0 || args[i].Get<int32_t>() == kMagicJitUserContextValue);
            store_value(Int(32), args[i], &scalars[i]);
            trampoline_args[i] = &scalars[i];
        } else if (a.is_buffer) {
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

    TrampolineFn trampoline_fn;
    std::vector<ExternArgType> arg_types;
    if (!build_extern_arg_types(fn_name, jit_externs, trampolines, trampoline_fn, arg_types)) {
        return wabt::interp::HostFunc::Ptr();
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
#endif  // WITH_WABT

#if WITH_V8
v8::Local<v8::String> NewLocalString(v8::Isolate *isolate, const char *s) {
    return v8::String::NewFromUtf8(isolate, s).ToLocalChecked();
}
// ------------------------------

template<typename T>
struct StoreScalar {
    void operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
        *(T *)slot = (T)val->NumberValue(context).ToChecked();
    }
};

template<>
inline void StoreScalar<float16_t>::operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
    float16_t f((double)val->NumberValue(context).ToChecked());
    *(uint16_t *)slot = f.to_bits();
}

template<>
inline void StoreScalar<bfloat16_t>::operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
    bfloat16_t b((double)val->NumberValue(context).ToChecked());
    *(uint16_t *)slot = b.to_bits();
}

template<>
inline void StoreScalar<void *>::operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
}

template<>
inline void StoreScalar<uint64_t>::operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
}

template<>
inline void StoreScalar<int64_t>::operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
}

void store_scalar(const Local<Context> &context, const Type &t, const Local<Value> &val, void *slot) {
    return dynamic_type_dispatch<StoreScalar>(t, context, val, slot);
}

template<typename T>
void store_scalar(const Local<Context> &context, const Local<Value> &val, void *slot) {
    return StoreScalar<T>()(context, val, slot);
}

// ------------------------------

template<typename T>
struct LoadAndReturnScalar {
    void operator()(const Local<Context> &context, const void *slot, ReturnValue<Value> val) {
        val.Set(*(const T *)slot);
    }
};

template<>
inline void LoadAndReturnScalar<float16_t>::operator()(const Local<Context> &context, const void *slot, ReturnValue<Value> val) {
    float16_t f = float16_t::make_from_bits(*(const uint16_t *)slot);
    val.Set((double)f);
}

template<>
inline void LoadAndReturnScalar<bfloat16_t>::operator()(const Local<Context> &context, const void *slot, ReturnValue<Value> val) {
    bfloat16_t b = bfloat16_t::make_from_bits(*(const uint16_t *)slot);
    val.Set((double)b);
}

template<>
inline void LoadAndReturnScalar<void *>::operator()(const Local<Context> &context, const void *slot, ReturnValue<Value> val) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
}

template<>
inline void LoadAndReturnScalar<uint64_t>::operator()(const Local<Context> &context, const void *slot, ReturnValue<Value> val) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
}

template<>
inline void LoadAndReturnScalar<int64_t>::operator()(const Local<Context> &context, const void *slot, ReturnValue<Value> val) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
}

// ------------------------------

template<typename T>
struct LoadScalar {
    Local<Value> operator()(const Local<Context> &context, const void *val_ptr) {
        double val = *(const T *)(val_ptr);
        Isolate *isolate = context->GetIsolate();
        return Number::New(isolate, val);
    }
};

template<>
inline Local<Value> LoadScalar<float16_t>::operator()(const Local<Context> &context, const void *val_ptr) {
    double val = (double)*(const uint16_t *)val_ptr;
    Isolate *isolate = context->GetIsolate();
    return Number::New(isolate, val);
}

template<>
inline Local<Value> LoadScalar<bfloat16_t>::operator()(const Local<Context> &context, const void *val_ptr) {
    double val = (double)*(const uint16_t *)val_ptr;
    Isolate *isolate = context->GetIsolate();
    return Number::New(isolate, val);
}

template<>
inline Local<Value> LoadScalar<void *>::operator()(const Local<Context> &context, const void *val_ptr) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
    return Local<Value>();
}

template<>
inline Local<Value> LoadScalar<uint64_t>::operator()(const Local<Context> &context, const void *val_ptr) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
    return Local<Value>();
}

template<>
inline Local<Value> LoadScalar<int64_t>::operator()(const Local<Context> &context, const void *val_ptr) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
    return Local<Value>();
}

// ------------------------------

Local<Value> load_scalar(const Local<Context> &context, const Type &t, const void *val_ptr) {
    return dynamic_type_dispatch<LoadScalar>(t, context, val_ptr);
}

template<typename T>
Local<Value> load_scalar(const Local<Context> &context, const T &val) {
    return LoadScalar<T>()(context, &val);
}

// ---------------------------------

enum EmbedderDataSlots {
    // don't use slot 0
    kWasmMemoryObject = 1,
    kBDMallocPtr,
    kHeapBase,
    kJitUserContext,
    kString_buffer,
    kString_grow,
};

wasm32_ptr_t v8_WasmMemoryObject_malloc(const Local<Context> &context, size_t size) {
    Isolate *isolate = context->GetIsolate();

    BDMalloc *bdmalloc = (BDMalloc *)context->GetAlignedPointerFromEmbedderData(kBDMallocPtr);
    if (!bdmalloc->inited()) {
        int32_t heap_base = context->GetEmbedderData(kHeapBase)->Int32Value(context).ToChecked();

        Local<Object> memory_value = context->GetEmbedderData(kWasmMemoryObject).As<Object>();  // really a WasmMemoryObject
        Local<Object> buffer_string = context->GetEmbedderData(kString_buffer).As<Object>();
        Local<ArrayBuffer> wasm_memory = Local<ArrayBuffer>::Cast(memory_value->Get(context, buffer_string).ToLocalChecked());

        wdebug(0) << "heap_base is: " << heap_base << "\n";
        wdebug(0) << "initial memory size is: " << wasm_memory->ByteLength() << "\n";
        bdmalloc->init(wasm_memory->ByteLength(), heap_base);
    }

    wasm32_ptr_t p = bdmalloc->alloc_region(size);
    if (!p) {
        Local<Object> memory_value = context->GetEmbedderData(kWasmMemoryObject).As<Object>();  // really a WasmMemoryObject

        constexpr int kWasmPageSize = 65536;
        const int32_t pages_needed = (size + kWasmPageSize - 1) / 65536;
        wdebug(0) << "attempting to grow by pages: " << pages_needed << "\n";

        Local<Value> args[1] = {Integer::New(isolate, pages_needed)};
        int32_t result = memory_value
                             ->Get(context, context->GetEmbedderData(kString_grow))
                             .ToLocalChecked()
                             .As<Object>()
                             ->CallAsFunction(context, memory_value, 1, args)
                             .ToLocalChecked()
                             ->Int32Value(context)
                             .ToChecked();
        wdebug(0) << "grow result: " << result << "\n";
        internal_assert(result == (int)(bdmalloc->get_total_size() / kWasmPageSize));

        Local<Object> buffer_string = context->GetEmbedderData(kString_buffer).As<Object>();
        Local<ArrayBuffer> wasm_memory = Local<ArrayBuffer>::Cast(memory_value->Get(context, buffer_string).ToLocalChecked());
        wdebug(0) << "New ArrayBuffer size is: " << wasm_memory->ByteLength() << "\n";

        bdmalloc->grow_total_size(wasm_memory->ByteLength());
        p = bdmalloc->alloc_region(size);
    }

    wdebug(2) << "allocation of " << size << " at: " << p << "\n";
    return p;
}

void v8_WasmMemoryObject_free(const Local<Context> &context, wasm32_ptr_t ptr) {
    wdebug(2) << "freeing ptr at: " << ptr << "\n";
    BDMalloc *bdmalloc = (BDMalloc *)context->GetAlignedPointerFromEmbedderData(kBDMallocPtr);
    bdmalloc->free_region(ptr);
}

uint8_t *get_wasm_memory_base(const Local<Context> &context) {
    Local<Object> memory_value = context->GetEmbedderData(kWasmMemoryObject).As<Object>();  // really a WasmMemoryObject
    Local<ArrayBuffer> wasm_memory = Local<ArrayBuffer>::Cast(memory_value->Get(context, context->GetEmbedderData(kString_buffer)).ToLocalChecked());
    std::shared_ptr<v8::BackingStore> backing = wasm_memory->GetBackingStore();
    uint8_t *p = (uint8_t *)backing->Data();
    return p;
}

void dump_hostbuf(const Local<Context> &context, const halide_buffer_t *buf, const std::string &label) {
#if WASM_DEBUG_LEVEL >= 2
    const halide_dimension_t *dim = buf->dim;
    const uint8_t *host = buf->host;

    wdebug(0) << label << " = " << (const void *)buf << " = {\n";
    wdebug(0) << "  device = " << buf->device << "\n";
    wdebug(0) << "  device_interface = " << buf->device_interface << "\n";
    wdebug(0) << "  host = " << (const void *)host << " = {\n";
    if (host) {
        wdebug(0) << "    " << (int)host[0] << ", " << (int)host[1] << ", " << (int)host[2] << ", " << (int)host[3] << "...\n";
    }
    wdebug(0) << "  }\n";
    wdebug(0) << "  flags = " << buf->flags << "\n";
    wdebug(0) << "  type = " << (int)buf->type.code << "," << (int)buf->type.bits << "," << buf->type.lanes << "\n";
    wdebug(0) << "  dimensions = " << buf->dimensions << "\n";
    wdebug(0) << "  dim = " << (void *)buf->dim << " = {\n";
    for (int i = 0; i < buf->dimensions; i++) {
        const auto &d = dim[i];
        wdebug(0) << "    {" << d.min << "," << d.extent << "," << d.stride << "," << d.flags << "},\n";
    }
    wdebug(0) << "  }\n";
    wdebug(0) << "  padding = " << buf->padding << "\n";
    wdebug(0) << "}\n";
#endif
}

void dump_wasmbuf(const Local<Context> &context, wasm32_ptr_t buf_ptr, const std::string &label) {
#if WASM_DEBUG_LEVEL >= 2
    internal_assert(buf_ptr);

    uint8_t *base = get_wasm_memory_base(context);
    wasm_halide_buffer_t *buf = (wasm_halide_buffer_t *)(base + buf_ptr);
    halide_dimension_t *dim = buf->dim ? (halide_dimension_t *)(base + buf->dim) : nullptr;
    uint8_t *host = buf->host ? (base + buf->host) : nullptr;

    wdebug(0) << label << " = " << buf_ptr << " -> " << (void *)buf << " = {\n";
    wdebug(0) << "  device = " << buf->device << "\n";
    wdebug(0) << "  device_interface = " << buf->device_interface << "\n";
    wdebug(0) << "  host = " << buf->host << " -> " << (void *)host << " = {\n";
    if (host) {
        wdebug(0) << "    " << (int)host[0] << ", " << (int)host[1] << ", " << (int)host[2] << ", " << (int)host[3] << "...\n";
    }
    wdebug(0) << "  }\n";
    wdebug(0) << "  flags = " << buf->flags << "\n";
    wdebug(0) << "  type = " << (int)buf->type.code << "," << (int)buf->type.bits << "," << buf->type.lanes << "\n";
    wdebug(0) << "  dimensions = " << buf->dimensions << "\n";
    wdebug(0) << "  dim = " << buf->dim << " -> " << (void *)dim << " = {\n";
    for (int i = 0; i < buf->dimensions; i++) {
        const auto &d = dim[i];
        wdebug(0) << "    {" << d.min << "," << d.extent << "," << d.stride << "," << d.flags << "},\n";
    }
    wdebug(0) << "  }\n";
    wdebug(0) << "  padding = " << buf->padding << "\n";
    wdebug(0) << "}\n";
#endif
}

// Given a halide_buffer_t on the host, allocate a wasm_halide_buffer_t in wasm
// memory space and copy all relevant data. The resulting buf is laid out in
// contiguous memory, and can be free with a single free().
wasm32_ptr_t hostbuf_to_wasmbuf(const Local<Context> &context, const halide_buffer_t *src) {
    static_assert(sizeof(halide_type_t) == 4, "halide_type_t");
    static_assert(sizeof(halide_dimension_t) == 16, "halide_dimension_t");
    static_assert(sizeof(wasm_halide_buffer_t) == 40, "wasm_halide_buffer_t");

    wdebug(0) << "\nhostbuf_to_wasmbuf:\n";
    if (!src) {
        return 0;
    }

    dump_hostbuf(context, src, "src");

    internal_assert(src->device == 0);
    internal_assert(src->device_interface == nullptr);

    // Assume our malloc() has everything 32-byte aligned,
    // and insert enough padding for host to also be 32-byte aligned.
    const size_t dims_size_in_bytes = sizeof(halide_dimension_t) * src->dimensions;
    const size_t dims_offset = sizeof(wasm_halide_buffer_t);
    const size_t mem_needed_base = sizeof(wasm_halide_buffer_t) + dims_size_in_bytes;
    const size_t host_offset = align_up(mem_needed_base);
    const size_t host_size_in_bytes = src->size_in_bytes();
    const size_t mem_needed = host_offset + host_size_in_bytes;

    const wasm32_ptr_t dst_ptr = v8_WasmMemoryObject_malloc(context, mem_needed);
    internal_assert(dst_ptr);

    uint8_t *base = get_wasm_memory_base(context);

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

    dump_wasmbuf(context, dst_ptr, "dst");

    return dst_ptr;
}

// Given a pointer to a wasm_halide_buffer_t in wasm memory space,
// allocate a Buffer<> on the host and copy all relevant data.
void wasmbuf_to_hostbuf(const Local<Context> &context, wasm32_ptr_t src_ptr, Halide::Runtime::Buffer<> &dst) {
    wdebug(0) << "\nwasmbuf_to_hostbuf:\n";
    dump_wasmbuf(context, src_ptr, "src");

    internal_assert(src_ptr);

    uint8_t *base = get_wasm_memory_base(context);

    wasm_halide_buffer_t *src = (wasm_halide_buffer_t *)(base + src_ptr);

    internal_assert(src->device == 0);
    internal_assert(src->device_interface == 0);

    halide_buffer_t dst_tmp;
    dst_tmp.device = 0;
    dst_tmp.device_interface = nullptr;
    dst_tmp.host = nullptr;  // src->host ? (base + src->host) : nullptr;
    dst_tmp.flags = src->flags;
    dst_tmp.type = src->type;
    dst_tmp.dimensions = src->dimensions;
    dst_tmp.dim = src->dim ? (halide_dimension_t *)(base + src->dim) : nullptr;
    dst_tmp.padding = nullptr;

    dump_hostbuf(context, &dst_tmp, "dst_tmp");

    dst = Halide::Runtime::Buffer<>(dst_tmp);
    if (src->host) {
        // Don't use dst.copy(); it can tweak strides in ways that matter.
        dst.allocate();
        const size_t host_size_in_bytes = dst.raw_buffer()->size_in_bytes();
        memcpy(dst.raw_buffer()->host, base + src->host, host_size_in_bytes);
    }
    dump_hostbuf(context, dst.raw_buffer(), "dst");
}

// Given a wasm_halide_buffer_t, copy possibly-changed data into a halide_buffer_t.
// Both buffers are asserted to match in type and dimensions.
void copy_wasmbuf_to_existing_hostbuf(const Local<Context> &context, wasm32_ptr_t src_ptr, halide_buffer_t *dst) {
    internal_assert(src_ptr && dst);

    wdebug(0) << "\ncopy_wasmbuf_to_existing_hostbuf:\n";
    dump_wasmbuf(context, src_ptr, "src");

    uint8_t *base = get_wasm_memory_base(context);

    wasm_halide_buffer_t *src = (wasm_halide_buffer_t *)(base + src_ptr);
    internal_assert(src->device == 0);
    internal_assert(src->device_interface == 0);
    internal_assert(src->dimensions == dst->dimensions);
    internal_assert(src->type == dst->type);

    dump_hostbuf(context, dst, "dst_pre");

    if (src->dimensions) {
        memcpy(dst->dim, base + src->dim, sizeof(halide_dimension_t) * src->dimensions);
    }
    if (src->host) {
        size_t host_size_in_bytes = dst->size_in_bytes();
        memcpy(dst->host, base + src->host, host_size_in_bytes);
    }

    dst->device = 0;
    dst->device_interface = nullptr;
    dst->flags = src->flags;

    dump_hostbuf(context, dst, "dst_post");
}

// Given a halide_buffer_t, copy possibly-changed data into a wasm_halide_buffer_t.
// Both buffers are asserted to match in type and dimensions.
void copy_hostbuf_to_existing_wasmbuf(const Local<Context> &context, const halide_buffer_t *src, wasm32_ptr_t dst_ptr) {
    internal_assert(src && dst_ptr);

    wdebug(0) << "\ncopy_hostbuf_to_existing_wasmbuf:\n";
    dump_hostbuf(context, src, "src");

    uint8_t *base = get_wasm_memory_base(context);

    wasm_halide_buffer_t *dst = (wasm_halide_buffer_t *)(base + dst_ptr);
    internal_assert(src->device == 0);
    internal_assert(src->device_interface == nullptr);
    internal_assert(src->dimensions == dst->dimensions);
    internal_assert(src->type == dst->type);

    dump_wasmbuf(context, dst_ptr, "dst_pre");

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

    dump_wasmbuf(context, dst_ptr, "dst_post");
}

JITUserContext *check_jit_user_context(JITUserContext *jit_user_context) {
    user_assert(!jit_user_context->handlers.custom_malloc &&
                !jit_user_context->handlers.custom_free)
        << "The WebAssembly JIT cannot support set_custom_allocator()";
    user_assert(!jit_user_context->handlers.custom_do_task)
        << "The WebAssembly JIT cannot support set_custom_do_task()";
    user_assert(!jit_user_context->handlers.custom_do_par_for)
        << "The WebAssembly JIT cannot support set_custom_do_par_for()";
    user_assert(!jit_user_context->handlers.custom_get_symbol &&
                !jit_user_context->handlers.custom_load_library &&
                !jit_user_context->handlers.custom_get_library_symbol)
        << "The WebAssembly JIT cannot support custom_get_symbol, custom_load_library, or custom_get_library_symbol.";
    return jit_user_context;
}

// Some internal code can call halide_error(null, ...), so this needs to be resilient to that.
// Callers must expect null and not crash.
JITUserContext *get_jit_user_context(const Local<Context> &context, const Local<Value> &arg) {
    int32_t ucon_magic = arg->Int32Value(context).ToChecked();
    if (ucon_magic == 0) {
        return nullptr;
    }
    internal_assert(ucon_magic == kMagicJitUserContextValue);
    JITUserContext *jit_user_context = (JITUserContext *)context->GetAlignedPointerFromEmbedderData(kJitUserContext);
    internal_assert(jit_user_context);
    return jit_user_context;
}

void wasm_jit_halide_print_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    JITUserContext *jit_user_context = get_jit_user_context(context, args[0]);
    const int32_t str_address = args[1]->Int32Value(context).ToChecked();

    uint8_t *p = get_wasm_memory_base(context);
    const char *str = (const char *)p + str_address;

    if (jit_user_context && jit_user_context->handlers.custom_print != nullptr) {
        (*jit_user_context->handlers.custom_print)(jit_user_context, str);
        debug(0) << str;
    } else {
        std::cout << str;
    }
}

void wasm_jit_halide_error_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    JITUserContext *jit_user_context = get_jit_user_context(context, args[0]);
    const int32_t str_address = args[1]->Int32Value(context).ToChecked();

    uint8_t *p = get_wasm_memory_base(context);
    const char *str = (const char *)p + str_address;

    if (jit_user_context && jit_user_context->handlers.custom_error != nullptr) {
        (*jit_user_context->handlers.custom_error)(jit_user_context, str);
    } else {
        halide_runtime_error << str;
    }
}

void wasm_jit_halide_trace_helper_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    internal_assert(args.Length() == 12);
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    uint8_t *base = get_wasm_memory_base(context);

    JITUserContext *jit_user_context = get_jit_user_context(context, args[0]);

    const wasm32_ptr_t func_name_ptr = args[1]->Int32Value(context).ToChecked();
    const wasm32_ptr_t value_ptr = args[2]->Int32Value(context).ToChecked();
    const wasm32_ptr_t coordinates_ptr = args[3]->Int32Value(context).ToChecked();
    const int type_code = args[4]->Int32Value(context).ToChecked();
    const int type_bits = args[5]->Int32Value(context).ToChecked();
    const int type_lanes = args[6]->Int32Value(context).ToChecked();
    const int trace_code = args[7]->Int32Value(context).ToChecked();
    const int parent_id = args[8]->Int32Value(context).ToChecked();
    const int value_index = args[9]->Int32Value(context).ToChecked();
    const int dimensions = args[10]->Int32Value(context).ToChecked();
    const wasm32_ptr_t trace_tag_ptr = args[11]->Int32Value(context).ToChecked();

    internal_assert(dimensions >= 0 && dimensions < 1024);  // not a hard limit, just a sanity check

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

    int result = 0;
    if (jit_user_context && jit_user_context->handlers.custom_trace != nullptr) {
        result = (*jit_user_context->handlers.custom_trace)(jit_user_context, &event);
    } else {
        debug(0) << "Dropping trace event due to lack of trace handler.\n";
    }

    args.GetReturnValue().Set(load_scalar(context, result));
}

void wasm_jit_malloc_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    size_t size = args[0]->Int32Value(context).ToChecked() + kExtraMallocSlop;
    wasm32_ptr_t p = v8_WasmMemoryObject_malloc(context, size);
    if (p) { p += kExtraMallocSlop; }
    args.GetReturnValue().Set(load_scalar(context, p));
}

void wasm_jit_free_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    wasm32_ptr_t p = args[0]->Int32Value(context).ToChecked();
    if (p) { p -= kExtraMallocSlop; }
    v8_WasmMemoryObject_free(context, p);
}

void wasm_jit_abort_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    abort();
}

void wasm_jit_strlen_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const int32_t s = args[0]->Int32Value(context).ToChecked();

    uint8_t *base = get_wasm_memory_base(context);
    int32_t r = strlen((char *)base + s);

    args.GetReturnValue().Set(load_scalar(context, r));
}

void wasm_jit_write_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    internal_error << "WebAssembly JIT does not yet support the write() call.";
}

void wasm_jit_getenv_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const int32_t s = args[0]->Int32Value(context).ToChecked();

    uint8_t *base = get_wasm_memory_base(context);
    char *e = getenv((char *)base + s);

    // TODO: this string is leaked
    if (e) {
        wasm32_ptr_t r = v8_WasmMemoryObject_malloc(context, strlen(e) + 1);
        strcpy((char *)base + r, e);
        args.GetReturnValue().Set(load_scalar(context, r));
    } else {
        args.GetReturnValue().Set(load_scalar<wasm32_ptr_t>(context, 0));
    }
}

void wasm_jit_memcpy_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const int32_t dst = args[0]->Int32Value(context).ToChecked();
    const int32_t src = args[1]->Int32Value(context).ToChecked();
    const int32_t n = args[2]->Int32Value(context).ToChecked();

    uint8_t *base = get_wasm_memory_base(context);

    memcpy(base + dst, base + src, n);

    args.GetReturnValue().Set(load_scalar(context, dst));
}

void wasm_jit_memmove_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const int32_t dst = args[0]->Int32Value(context).ToChecked();
    const int32_t src = args[1]->Int32Value(context).ToChecked();
    const int32_t n = args[2]->Int32Value(context).ToChecked();

    uint8_t *base = get_wasm_memory_base(context);

    memmove(base + dst, base + src, n);

    args.GetReturnValue().Set(load_scalar(context, dst));
}

void wasm_jit_fopen_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    internal_error << "WebAssembly JIT does not yet support the fopen() call.";
}

void wasm_jit_fileno_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    internal_error << "WebAssembly JIT does not yet support the fileno() call.";
}

void wasm_jit_fclose_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    internal_error << "WebAssembly JIT does not yet support the fclose() call.";
}

void wasm_jit_fwrite_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    internal_error << "WebAssembly JIT does not yet support the fwrite() call.";
}

void wasm_jit_memset_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const int32_t s = args[0]->Int32Value(context).ToChecked();
    const int32_t c = args[1]->Int32Value(context).ToChecked();
    const int32_t n = args[2]->Int32Value(context).ToChecked();

    uint8_t *base = get_wasm_memory_base(context);
    memset(base + s, c, n);

    args.GetReturnValue().Set(load_scalar(context, s));
}

void wasm_jit_memcmp_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const int32_t s1 = args[0]->Int32Value(context).ToChecked();
    const int32_t s2 = args[1]->Int32Value(context).ToChecked();
    const int32_t n = args[2]->Int32Value(context).ToChecked();

    uint8_t *base = get_wasm_memory_base(context);

    int r = memcmp(base + s1, base + s2, n);

    args.GetReturnValue().Set(load_scalar(context, r));
}

void wasm_jit___cxa_atexit_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    // nothing
}

void wasm_jit___extendhfsf2_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const uint16_t in = args[0]->NumberValue(context).ToChecked();
    const float out = (float)float16_t::make_from_bits(in);

    args.GetReturnValue().Set(load_scalar(context, out));
}

void wasm_jit___truncsfhf2_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const float in = args[0]->NumberValue(context).ToChecked();
    const uint16_t out = float16_t(in).to_bits();

    args.GetReturnValue().Set(load_scalar(context, out));
}

template<typename T, T some_func(T)>
void wasm_jit_posix_math_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const T in = args[0]->NumberValue(context).ToChecked();
    const T out = some_func(in);

    args.GetReturnValue().Set(load_scalar(context, out));
}

template<typename T, T some_func(T, T)>
void wasm_jit_posix_math2_callback(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const T in1 = args[0]->NumberValue(context).ToChecked();
    const T in2 = args[1]->NumberValue(context).ToChecked();
    const T out = some_func(in1, in2);

    args.GetReturnValue().Set(load_scalar(context, out));
}

enum ExternWrapperFieldSlots {
    kTrampolineWrap,
    kArgTypesWrap
};

void v8_extern_wrapper(const v8::FunctionCallbackInfo<v8::Value> &args) {
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> wrapper_data = args.Data()->ToObject(context).ToLocalChecked();

    Local<External> trampoline_wrap = Local<External>::Cast(wrapper_data->GetInternalField(kTrampolineWrap));
    Local<ArrayBuffer> arg_types_wrap = Local<ArrayBuffer>::Cast(wrapper_data->GetInternalField(kArgTypesWrap));

    TrampolineFn trampoline = (TrampolineFn)trampoline_wrap->Value();

    size_t arg_types_len = (arg_types_wrap->ByteLength() / sizeof(ExternArgType)) - 1;
    std::shared_ptr<v8::BackingStore> backing = arg_types_wrap->GetBackingStore();
    ExternArgType *arg_types = (ExternArgType *)backing->Data();
    /* const ExternArgType *arg_types = (const ExternArgType *)arg_types_wrap->GetContents().Data(); */
    const ExternArgType ret_type = *arg_types++;

    // There's wasted space here, but that's ok.
    std::vector<Halide::Runtime::Buffer<>> buffers(arg_types_len);
    std::vector<uint64_t> scalars(arg_types_len);
    std::vector<void *> trampoline_args(arg_types_len);

    for (size_t i = 0; i < arg_types_len; ++i) {
        if (arg_types[i].is_ucon) {
            // We have to special-case ucon because Halide considers it an int64 everywhere
            // (even for wasm, where pointers are int32), and trying to extract it as an
            // int64 from a Value that is int32 will assert-fail. In JIT mode the value
            // doesn't even matter (except for guarding that it is our predicted constant).
            wassert(args[i].Get<int32_t>() == 0 || args[i].Get<int32_t>() == kMagicJitUserContextValue);
            store_scalar<int32_t>(context, args[i], &scalars[i]);
            trampoline_args[i] = &scalars[i];
        } else if (arg_types[i].is_buffer) {
            const wasm32_ptr_t buf_ptr = args[i]->Int32Value(context).ToChecked();
            wasmbuf_to_hostbuf(context, buf_ptr, buffers[i]);
            trampoline_args[i] = buffers[i].raw_buffer();
        } else {
            store_scalar(context, arg_types[i].type, args[i], &scalars[i]);
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
    (*trampoline)(trampoline_args.data());

    if (has_retval) {
        dynamic_type_dispatch<LoadAndReturnScalar>(ret_type.type, context, (void *)&ret_val, args.GetReturnValue());
    }

    // Progagate buffer data backwards. Note that for arbitrary extern functions,
    // we have no idea which buffers might be "input only", so we copy all data for all of them.
    for (size_t i = 0; i < arg_types_len; ++i) {
        if (arg_types[i].is_buffer) {
            const wasm32_ptr_t buf_ptr = args[i]->Int32Value(context).ToChecked();
            copy_hostbuf_to_existing_wasmbuf(context, buffers[i], buf_ptr);
        }
    }
}

bool should_skip_extern_symbol(const std::string &name) {
    static std::set<std::string> symbols = {
        "halide_print",
        "halide_error"};
    return symbols.count(name) > 0;
}

using JITExternMap = std::map<std::string, Halide::JITExtern>;
void add_extern_callbacks(const Local<Context> &context,
                          const JITExternMap &jit_externs,
                          const JITModule &trampolines,
                          Local<Object> &imports_dict) {
    Isolate *isolate = context->GetIsolate();
    Local<ObjectTemplate> extern_callback_template = ObjectTemplate::New(isolate);
    extern_callback_template->SetInternalFieldCount(4);
    for (const auto &it : jit_externs) {
        const auto &fn_name = it.first;
        if (should_skip_extern_symbol(fn_name)) {
            continue;
        }

        TrampolineFn trampoline_fn;
        std::vector<ExternArgType> arg_types;
        if (!build_extern_arg_types(fn_name, jit_externs, trampolines, trampoline_fn, arg_types)) {
            internal_error << "Missing fn_name " << fn_name;
        }

        const size_t arg_types_bytes = sizeof(ExternArgType) * arg_types.size();
        Local<ArrayBuffer> arg_types_wrap = ArrayBuffer::New(isolate, arg_types_bytes);
        std::shared_ptr<v8::BackingStore> backing = arg_types_wrap->GetBackingStore();
        memcpy((ExternArgType *)backing->Data(), arg_types.data(), arg_types_bytes);

        Local<Object> wrapper_data = extern_callback_template->NewInstance(context).ToLocalChecked();
        static_assert(sizeof(trampoline_fn) == sizeof(void *));
        Local<External> trampoline_wrap(External::New(isolate, (void *)trampoline_fn));
        wrapper_data->SetInternalField(kTrampolineWrap, trampoline_wrap);
        wrapper_data->SetInternalField(kArgTypesWrap, arg_types_wrap);

        Local<v8::String> key = NewLocalString(isolate, fn_name.c_str());
        Local<v8::Function> value = FunctionTemplate::New(isolate, v8_extern_wrapper, wrapper_data)
                                        ->GetFunction(context)
                                        .ToLocalChecked();

        (void)imports_dict->Set(context, key, value).ToChecked();
    }
}

#endif  // WITH_V8

}  // namespace

// clang-format off

#if WITH_WABT
using HostCallbackMap = std::unordered_map<std::string, wabt::interp::HostFunc::Callback>;

#define DEFINE_CALLBACK(f)                { #f, wabt_jit_##f##_callback },
#define DEFINE_POSIX_MATH_CALLBACK(t, f)  { #f, wabt_posix_math_1<t, ::f> },
#define DEFINE_POSIX_MATH_CALLBACK2(t, f) { #f, wabt_posix_math_2<t, ::f> },

#endif

#ifdef WITH_V8
using HostCallbackMap = std::unordered_map<std::string, FunctionCallback>;

#define DEFINE_CALLBACK(f)                { #f, wasm_jit_##f##_callback },
#define DEFINE_POSIX_MATH_CALLBACK(t, f)  { #f, wasm_jit_posix_math_callback<t, ::f> },
#define DEFINE_POSIX_MATH_CALLBACK2(t, f)  { #f, wasm_jit_posix_math2_callback<t, ::f> },
#endif

const HostCallbackMap &get_host_callback_map() {

    static HostCallbackMap m = {
        // General runtime functions.

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
        DEFINE_CALLBACK(memmove)
        DEFINE_CALLBACK(memset)
        DEFINE_CALLBACK(strlen)
        DEFINE_CALLBACK(write)

        // Posix math.
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

        DEFINE_POSIX_MATH_CALLBACK2(float, atan2f)
        DEFINE_POSIX_MATH_CALLBACK2(double, atan2)
        DEFINE_POSIX_MATH_CALLBACK2(float, fminf)
        DEFINE_POSIX_MATH_CALLBACK2(double, fmin)
        DEFINE_POSIX_MATH_CALLBACK2(float, fmaxf)
        DEFINE_POSIX_MATH_CALLBACK2(double, fmax)
        DEFINE_POSIX_MATH_CALLBACK2(float, powf)
        DEFINE_POSIX_MATH_CALLBACK2(double, pow)
    };

    return m;
}

#undef DEFINE_CALLBACK
#undef DEFINE_POSIX_MATH_CALLBACK
#undef DEFINE_POSIX_MATH_CALLBACK2

// clang-format on

#endif  // WITH_WABT || WITH_V8

struct WasmModuleContents {
    mutable RefCount ref_count;

    const Target target;
    const std::vector<Argument> arguments;
    std::map<std::string, Halide::JITExtern> jit_externs;
    std::vector<JITModule> extern_deps;
    JITModule trampolines;

#if WITH_WABT || WITH_V8
    BDMalloc bdmalloc;
#endif  // WITH_WABT || WITH_V8

#if WITH_WABT
    wabt::interp::Store store;
    wabt::interp::Module::Ptr module;
    wabt::interp::Instance::Ptr instance;
    wabt::interp::Thread::Options thread_options;
    wabt::interp::Memory::Ptr memory;
#endif

#ifdef WITH_V8
    v8::Isolate *isolate = nullptr;
    v8::ArrayBuffer::Allocator *array_buffer_allocator = nullptr;
    v8::Persistent<v8::Context> v8_context;
    v8::Persistent<v8::Function> v8_function;
#endif

    WasmModuleContents(
        const Module &halide_module,
        const std::vector<Argument> &arguments,
        const std::string &fn_name,
        const std::map<std::string, Halide::JITExtern> &jit_externs,
        const std::vector<JITModule> &extern_deps);

    int run(const void **args);

    ~WasmModuleContents() = default;
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

#if WITH_WABT || WITH_V8
    wdebug(1) << "Compiling wasm function " << fn_name << "\n";
#endif  // WITH_WABT || WITH_V8

#if WITH_WABT
    user_assert(!target.has_feature(Target::WasmThreads)) << "wasm_threads requires Emscripten (or a similar compiler); it will never be supported under JIT.";

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
    wabt::Result r = wabt::interp::ReadBinaryInterp("<internal>",
                                                    final_wasm.data(),
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

#ifdef WITH_V8
    static std::once_flag init_v8_once;
    std::call_once(init_v8_once, []() {
        // Initialize V8.
        V8::InitializeICU();
        static std::unique_ptr<Platform> platform = platform::NewDefaultPlatform();
        V8::InitializePlatform(platform.get());
        V8::Initialize();
        std::vector<std::string> flags = {
            // TODO: these need to match the flags we set in CodeGen_WebAssembly::mattrs().
            // Note that we currently enable all features that *might* be used
            // (eg we enable simd even though we might not use it) as we may well end
            // using different Halide Targets across our lifespan.

            // Sometimes useful for debugging purposes:
            // "--print_all_exceptions=true",
            // "--abort_on_uncaught_exception",
            // "--trace-ignition-codegen",
            // "--trace_wasm_decoder",
            // "--no-liftoff",
            // "--wasm-interpret-all",
            // "--trace-wasm-memory",
        };
        for (const auto &f : flags) {
            V8::SetFlagsFromString(f.c_str(), f.size());
        }
    });

    array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();

    Isolate::CreateParams isolate_params;
    isolate_params.snapshot_blob = nullptr;
    isolate_params.array_buffer_allocator = array_buffer_allocator;
    // Create a new Isolate and make it the current one.
    isolate = Isolate::New(isolate_params);

    Locker locker(isolate);
    Isolate::Scope isolate_scope(isolate);

    // Create a stack-allocated handle scope.
    HandleScope handle_scope(isolate);

    Local<ObjectTemplate> global = ObjectTemplate::New(isolate);
    Local<Context> context = Context::New(isolate, nullptr, global);
    v8_context.Reset(isolate, context);

    Context::Scope context_scope(context);

    TryCatch try_catch(isolate);
    try_catch.SetCaptureMessage(true);
    try_catch.SetVerbose(true);

    Local<v8::String> fn_name_str = NewLocalString(isolate, fn_name.c_str());

    std::vector<char> final_wasm = compile_to_wasm(halide_module, fn_name);

    MaybeLocal<WasmModuleObject> maybe_compiled = WasmModuleObject::Compile(
        isolate,
        /* wire_bytes */ {(const uint8_t *)final_wasm.data(), final_wasm.size()});

    Local<WasmModuleObject> compiled;
    if (!maybe_compiled.ToLocal(&compiled)) {
        // Versions of V8 prior to 7.5 or so don't propagate the exception properly,
        // so don't attempt to print the exception info if it's not present.
        if (try_catch.HasCaught()) {
            String::Utf8Value error(isolate, try_catch.Exception());
            internal_error << "Error compiling wasm: " << *error << "\n";
        } else {
            internal_error << "Error compiling wasm: <unknown>\n";
        }
    }

    const HostCallbackMap &host_callback_map = get_host_callback_map();
    Local<Object> imports_dict = Object::New(isolate);
    for (const auto &it : host_callback_map) {
        const std::string &name = it.first;
        FunctionCallback f = it.second;
        Local<v8::String> key = NewLocalString(isolate, name.c_str());
        Local<v8::Function> value = FunctionTemplate::New(isolate, f)->GetFunction(context).ToLocalChecked();
        (void)imports_dict->Set(context, key, value).ToChecked();
    };

    add_extern_callbacks(context, jit_externs, trampolines, imports_dict);

    Local<Object> imports = Object::New(isolate);
    (void)imports->Set(context, NewLocalString(isolate, "env"), imports_dict).ToChecked();

    Local<Value> instance_args[2] = {compiled, imports};

    Local<Object> exports = context->Global()
                                ->Get(context, NewLocalString(isolate, "WebAssembly"))
                                .ToLocalChecked()
                                .As<Object>()
                                ->Get(context, NewLocalString(isolate, "Instance"))
                                .ToLocalChecked()
                                .As<Object>()
                                ->CallAsConstructor(context, 2, instance_args)
                                .ToLocalChecked()
                                .As<Object>()
                                ->Get(context, NewLocalString(isolate, "exports"))
                                .ToLocalChecked()
                                .As<Object>();

    Local<Value> function_value = exports->Get(context, fn_name_str).ToLocalChecked();
    Local<v8::Function> function = Local<v8::Function>::Cast(function_value);
    internal_assert(!function.IsEmpty());
    internal_assert(!function->IsNullOrUndefined());
    v8_function.Reset(isolate, function);

    context->SetEmbedderData(kWasmMemoryObject, exports->Get(context, NewLocalString(isolate, "memory")).ToLocalChecked().As<Object>());
    context->SetAlignedPointerInEmbedderData(kBDMallocPtr, &bdmalloc);
    context->SetEmbedderData(kHeapBase, exports->Get(context, NewLocalString(isolate, "__heap_base")).ToLocalChecked().As<Object>());
    context->SetEmbedderData(kString_buffer, NewLocalString(isolate, "buffer"));
    context->SetEmbedderData(kString_grow, NewLocalString(isolate, "grow"));

    internal_assert(!try_catch.HasCaught());
#endif
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
    internal_assert(instance->host_info() == nullptr);
    instance->set_host_info(&wabt_context);

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

    wabt::interp::Thread thread(store);

    auto r = func->Call(thread, wabt_args, wabt_results, &trap);
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

    instance->set_host_info(nullptr);
    return result;

#endif

#if WITH_V8
    Locker locker(isolate);
    Isolate::Scope isolate_scope(isolate);

    // Create a stack-allocated handle scope.
    HandleScope handle_scope(isolate);

    Local<Context> context = Local<Context>::New(isolate, v8_context);
    // Enter the context for compiling and running the hello world script.
    Context::Scope context_scope(context);

    TryCatch try_catch(isolate);
    try_catch.SetCaptureMessage(true);
    try_catch.SetVerbose(true);

    std::vector<wasm32_ptr_t> wbufs(arguments.size(), 0);

    std::vector<v8::Local<Value>> js_args;
    for (size_t i = 0; i < arguments.size(); i++) {
        const Argument &arg = arguments[i];
        const void *arg_ptr = args[i];
        if (arg.is_buffer()) {
            halide_buffer_t *buf = (halide_buffer_t *)const_cast<void *>(arg_ptr);
            // It's OK for this to be null (let Halide asserts handle it)
            wasm32_ptr_t wbuf = hostbuf_to_wasmbuf(context, buf);
            wbufs[i] = wbuf;
            js_args.push_back(load_scalar(context, wbuf));
        } else {
            if (arg.name == "__user_context") {
                js_args.push_back(load_scalar(context, kMagicJitUserContextValue));
                JITUserContext *jit_user_context = check_jit_user_context(*(JITUserContext **)const_cast<void *>(arg_ptr));
                context->SetAlignedPointerInEmbedderData(kJitUserContext, jit_user_context);
            } else {
                js_args.push_back(load_scalar(context, arg.type, arg_ptr));
            }
        }
    }

    Local<v8::Function> function = Local<v8::Function>::New(isolate, v8_function);
    MaybeLocal<Value> result = function->Call(context, context->Global(), js_args.size(), js_args.data());

    if (result.IsEmpty()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        String::Utf8Value message(isolate, try_catch.Message()->GetSourceLine(context).ToLocalChecked());

        internal_error << "Error running wasm: " << *error << " | Line: " << *message << "\n";
    }

    int r = result.ToLocalChecked()->Int32Value(context).ToChecked();
    if (r == 0) {
        // Update any output buffers
        for (size_t i = 0; i < arguments.size(); i++) {
            const Argument &arg = arguments[i];
            const void *arg_ptr = args[i];
            if (arg.is_buffer()) {
                halide_buffer_t *buf = (halide_buffer_t *)const_cast<void *>(arg_ptr);
                copy_wasmbuf_to_existing_hostbuf(context, wbufs[i], buf);
            }
        }
    }

    for (wasm32_ptr_t p : wbufs) {
        v8_WasmMemoryObject_free(context, p);
    }

    // Don't do this: things allocated by Halide runtime might need to persist
    // between multiple invocations of the same function.
    // bdmalloc.reset();

    return r;
#endif  // WITH_V8

    internal_error << "WasmExecutor is not configured correctly";
    return -1;
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
#if WITH_WABT || WITH_V8
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
#if defined(WITH_WABT) || defined(WITH_V8)
    WasmModule wasm_module;
    wasm_module.contents = new WasmModuleContents(module, arguments, fn_name, jit_externs, extern_deps);
    return wasm_module;
#else
    user_error << "Cannot run JITted WebAssembly without configuring a WebAssembly engine.";
    return WasmModule();
#endif
}

/** Run generated previously compiled wasm code with a set of arguments. */
int WasmModule::run(const void **args) {
    internal_assert(contents.defined());
    return contents->run(args);
}

}  // namespace Internal
}  // namespace Halide
