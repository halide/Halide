#include "WasmExecutor.h"

#include "Error.h"
#include "Func.h"
#include "ImageParam.h"
#include "JITModule.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"
#include "Target.h"

#include <cmath>
#include <mutex>
#include <sstream>
#include <vector>

#ifdef WITH_V8
#include "v8.h"
#include "libplatform/libplatform.h"
#endif

// ---------------------

// Debugging the WebAssembly JIT support is usually disconnected from the rest of HL_DEBUG_CODEGEN
#define WASM_DEBUG_LEVEL 0

namespace {
    struct debug_sink {
        inline debug_sink() {}

        template<typename T>
        inline debug_sink &operator<<(T&& x) {
            return *this;
        }
    };
}  // namespace

#if WASM_DEBUG_LEVEL
    #define wdebug(x) Halide::Internal::debug(((x)<=WASM_DEBUG_LEVEL) ? 0 : 255)
#else
    #define wdebug(x) debug_sink()
#endif

// ---------------------

namespace {

template<typename T>
inline T align_up(T p, int alignment = 32) {
    return (p + alignment - 1) & ~(alignment - 1);
}

// Debugging our Malloc is extremely noisy and usually undesired

#define BDMALLOC_DEBUG_LEVEL 0
#if BDMALLOC_DEBUG_LEVEL
    #define bddebug(x) Halide::Internal::debug(((x)<=BDMALLOC_DEBUG_LEVEL) ? 0 : 255)
#else
    #define bddebug(x) debug_sink()
#endif

// BDMalloc aka BrainDeadMalloc. This is an *extremely* simple-minded implementation
// of malloc/free on top of a WasmMemoryObject, and is intended to be just barely adequate
// to allow Halide's JIT-based tests to pass. It is neither memory-efficient nor performant,
// nor has it been particularly well-vetted for potential buffer overruns and such.
class BDMalloc {
    struct Region {
        uint32_t size:31;
        uint32_t used:1;
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
        const uint32_t size = std::max(align_up((uint32_t) requested_size, kAlignment), kAlignment);

        constexpr uint32_t kMaxAllocSize = 0x7fffffff;
        internal_assert(size <= kMaxAllocSize);
        bddebug(2) << "size -> " << size << "\n";

        for (auto it = regions.begin(); it != regions.end(); it++) {
            const uint32_t start = it->first;
            Region &r = it->second;
            if (!r.used && r.size >= size) {
                bddebug(2) << "alloc @ " << start << "," << (uint32_t) r.size << "\n";
                if (r.size > size + kAlignment) {
                    // Split the block
                    const uint32_t r2_start = start + size;
                    const uint32_t r2_size = r.size - size;
                    regions[r2_start] = { r2_size, false };
                    r.size = size;
                    bddebug(2) << "split: r-> " << start << "," << (uint32_t) r.size << "," << (start + r.size) << "\n";
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
            bddebug(2) << "combine next: " << next->first << " w/ " << it->first << " " << "\n";
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
        internal_assert(start + r.size == new_total_size);
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
            bddebug(2) << "R: " << start << ".." << (start+r.size-1) << "," << r.used << "\n";
            internal_assert(start == prev_end) << "start " << start << " prev_end " << prev_end << "\n";
            // it's OK to have two used regions in a row, but not two free ones
            internal_assert(!(!prev_used && !r.used));
            prev_end = start + r.size;
            prev_used = r.used;
        }
        internal_assert(prev_end == total_size) << "prev_end " << prev_end << " total_size " << total_size << "\n";
        bddebug(2) << "\n";
#endif
    }

};

// ---------------------

// Trampolines do not use "_argv" as the suffix because
// that name may already exist and if so, will return an int
// instead of taking a pointer at the end of the args list to
// receive the result value.
static const char kTrampolineSuffix[] = "_trampoline";

using JITExternMap = std::map<std::string, Halide::JITExtern>;

}  // namespace

#ifdef WITH_V8

#define V8_API_VERSION ((V8_MAJOR_VERSION * 10) + V8_MINOR_VERSION)

static_assert(V8_API_VERSION >= 70,
              "Halide requires V8 v7.0 or later when compiling WITH_V8.");

namespace Halide {
namespace Internal {
namespace {

using namespace v8;

using wasm32_ptr_t = int32_t;

const wasm32_ptr_t kMagicJitUserContextValue = -1;

#if WASM_DEBUG_LEVEL
void print_object_properties(Isolate *isolate, const Local<Value> &v) {
    Local<Context> context = isolate->GetCurrentContext();
    String::Utf8Value objascii(isolate, v);
    wdebug(0) << *objascii << "\n";

    if (v->IsObject()) {
        Local<Object> obj = v.As<Object>();
        Local<Array> properties = obj->GetPropertyNames(context).ToLocalChecked();
        int len = properties->Length();
        wdebug(0) << "Number of properties = " << len << ":\n";
        for(int i = 0 ; i < len ; ++i) {
            const v8::Local<v8::Value> key = properties->Get(i);
            String::Utf8Value str(isolate, key);
            wdebug(0) << "\t" << i + 1 << ". " << *str << "\n";
        }
    }
}
#endif

inline constexpr int halide_type_code(halide_type_code_t code, int bits) {
    return ((int) code) | (bits << 8);
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
template<template<typename> class Functor, typename... Args>
auto dynamic_type_dispatch(const halide_type_t &type, Args&&... args) ->
    decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...)) {

#define HANDLE_CASE(CODE, BITS, TYPE) \
    case halide_type_code(CODE, BITS): return Functor<TYPE>()(std::forward<Args>(args)...);
    switch (halide_type_code((halide_type_code_t) type.code, type.bits)) {
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
        HANDLE_CASE(halide_type_handle, 64, void*)
        default:
            internal_error;
            using ReturnType = decltype(std::declval<Functor<uint8_t>>()(std::forward<Args>(args)...));
            return ReturnType();
    }
#undef HANDLE_CASE
}

// ------------------------------

template<typename T>
struct ExtractAndStoreScalar {
    void operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
        *(T *)slot = (T) val->NumberValue(context).ToChecked();
    }
};

template<>
inline void ExtractAndStoreScalar<void*>::operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
}

template<>
inline void ExtractAndStoreScalar<uint64_t>::operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
}

template<>
inline void ExtractAndStoreScalar<int64_t>::operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
}

// ------------------------------

template<typename T>
struct LoadAndReturnScalar {
    void operator()(const Local<Context> &context, const void *slot, ReturnValue<Value> val) {
        val.Set(*(const T *)slot);
    }
};

template<>
inline void LoadAndReturnScalar<void*>::operator()(const Local<Context> &context, const void *slot, ReturnValue<Value> val) {
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
struct WrapScalar {
    Local<Value> operator()(const Local<Context> &context, const void *val_ptr) {
        double val = *(const T *)(val_ptr);
        Isolate *isolate = context->GetIsolate();
        return Number::New(isolate, val);
    }
};

template<>
inline Local<Value> WrapScalar<void*>::operator()(const Local<Context> &context, const void *val_ptr) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
    return Local<Value>();
}

template<>
inline Local<Value> WrapScalar<uint64_t>::operator()(const Local<Context> &context, const void *val_ptr) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
    return Local<Value>();
}

template<>
inline Local<Value> WrapScalar<int64_t>::operator()(const Local<Context> &context, const void *val_ptr) {
    internal_error << "TODO: 64-bit slots aren't yet supported";
    return Local<Value>();
}

// ------------------------------

Local<Value> wrap_scalar(const Local<Context> &context, const Type &t, const void *val_ptr) {
    return dynamic_type_dispatch<WrapScalar>(t, context, val_ptr);
}

template<typename T>
Local<Value> wrap_scalar(const Local<Context> &context, const T &val) {
    return WrapScalar<T>()(context, &val);
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

    BDMalloc *bdmalloc = (BDMalloc *) context->GetAlignedPointerFromEmbedderData(kBDMallocPtr);
    if (!bdmalloc->inited()) {
        int32_t heap_base = context->GetEmbedderData(kHeapBase)->Int32Value(context).ToChecked();

        Local<Object> memory_value = context->GetEmbedderData(kWasmMemoryObject).As<Object>();  // really a WasmMemoryObject
        Local<Object> buffer_string = context->GetEmbedderData(kString_buffer).As<Object>();
        Local<ArrayBuffer> wasm_memory = Local<ArrayBuffer>::Cast(memory_value->Get(buffer_string));

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
            ->Get(context, context->GetEmbedderData(kString_grow)).ToLocalChecked().As<Object>()
            ->CallAsFunction(context, memory_value, 1, args).ToLocalChecked()->Int32Value(context).ToChecked();
        wdebug(0) << "grow result: " << result << "\n";
        internal_assert(result == (int) (bdmalloc->get_total_size() / kWasmPageSize));

        Local<Object> buffer_string = context->GetEmbedderData(kString_buffer).As<Object>();
        Local<ArrayBuffer> wasm_memory = Local<ArrayBuffer>::Cast(memory_value->Get(buffer_string));
        wdebug(0) << "New ArrayBuffer size is: " << wasm_memory->ByteLength() << "\n";

        bdmalloc->grow_total_size(wasm_memory->ByteLength());
        p = bdmalloc->alloc_region(size);
    }

    wdebug(2) << "allocation of " << size << " at: " << p << "\n";
    return p;
}

void v8_WasmMemoryObject_free(const Local<Context> &context, wasm32_ptr_t ptr) {
    wdebug(2) << "freeing ptr at: " << ptr << "\n";
    BDMalloc *bdmalloc = (BDMalloc *) context->GetAlignedPointerFromEmbedderData(kBDMallocPtr);
    bdmalloc->free_region(ptr);
}

uint8_t *get_wasm_memory_base(const Local<Context> &context) {
    Local<Object> memory_value = context->GetEmbedderData(kWasmMemoryObject).As<Object>();  // really a WasmMemoryObject
    Local<ArrayBuffer> wasm_memory = Local<ArrayBuffer>::Cast(memory_value->Get(context->GetEmbedderData(kString_buffer)));
    uint8_t *p = (uint8_t *) wasm_memory->GetContents().Data();
    return p;
}

struct wasm_halide_buffer_t {
    uint64_t device;
    wasm32_ptr_t device_interface;  // halide_device_interface_t*
    wasm32_ptr_t host;              // uint8_t*
    uint64_t flags;
    halide_type_t type;
    int32_t dimensions;
    wasm32_ptr_t dim;               // halide_dimension_t*
    wasm32_ptr_t padding;           // always zero
};

void dump_hostbuf(const Local<Context> &context, const halide_buffer_t *buf, const std::string &label) {
#if WASM_DEBUG_LEVEL >= 2
    const halide_dimension_t *dim = buf->dim;
    const uint8_t *host = buf->host;

    wdebug(0) << label << " = " << (void *) buf << " = {\n";
    wdebug(0) << "  device = " << buf->device << "\n";
    wdebug(0) << "  device_interface = " << buf->device_interface << "\n";
    wdebug(0) << "  host = " << (void *) host << " = {\n";
    if (host) {
        wdebug(0) << "    " << (int) host[0] << ", " << (int) host[1] << ", " << (int) host[2] << ", " << (int) host[3] << "...\n" ;
    }
    wdebug(0) << "  }\n";
    wdebug(0) << "  flags = " << buf->flags << "\n";
    wdebug(0) << "  type = " << (int) buf->type.code << "," << (int) buf->type.bits << "," << buf->type.lanes << "\n";
    wdebug(0) << "  dimensions = " << buf->dimensions << "\n";
    wdebug(0) << "  dim = " << (void*) buf->dim << " = {\n";
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
    wasm_halide_buffer_t *buf = (wasm_halide_buffer_t *) (base + buf_ptr);
    halide_dimension_t *dim = buf->dim ? (halide_dimension_t *) (base + buf->dim) : nullptr;
    uint8_t *host = buf->host ? (base + buf->host) : nullptr;

    wdebug(0) << label << " = " << buf_ptr << " -> " << (void *) buf << " = {\n";
    wdebug(0) << "  device = " << buf->device << "\n";
    wdebug(0) << "  device_interface = " << buf->device_interface << "\n";
    wdebug(0) << "  host = " << buf->host << " -> " << (void *) host << " = {\n";
    if (host) {
        wdebug(0) << "    " << (int) host[0] << ", " << (int) host[1] << ", " << (int) host[2] << ", " << (int) host[3] << "...\n" ;
    }
    wdebug(0) << "  }\n";
    wdebug(0) << "  flags = " << buf->flags << "\n";
    wdebug(0) << "  type = " << (int) buf->type.code << "," << (int) buf->type.bits << "," << buf->type.lanes << "\n";
    wdebug(0) << "  dimensions = " << buf->dimensions << "\n";
    wdebug(0) << "  dim = " << buf->dim << " -> " << (void*) dim << " = {\n";
    for (int i = 0; i < buf->dimensions; i++) {
        const auto &d = dim[i];
        wdebug(0) << "    {" << d.min << "," << d.extent << "," << d.stride << "," << d.flags << "},\n";
    }
    wdebug(0) << "  }\n";
    wdebug(0) << "  padding = " << buf->padding << "\n";
    wdebug(0) << "}\n";
#endif
}

static_assert(sizeof(halide_type_t) == 4, "halide_type_t");
static_assert(sizeof(halide_dimension_t) == 16, "halide_dimension_t");
static_assert(sizeof(wasm_halide_buffer_t) == 40, "wasm_halide_buffer_t");

// Given a halide_buffer_t on the host, allocate a wasm_halide_buffer_t in wasm
// memory space and copy all relevant data. The resulting buf is laid out in
// contiguous memory, and can be free with a single free().
wasm32_ptr_t hostbuf_to_wasmbuf(const Local<Context> &context, const halide_buffer_t *src) {
    static_assert(sizeof(halide_type_t) == 4, "halide_type_t");
    static_assert(sizeof(halide_dimension_t) == 16, "halide_dimension_t");
    static_assert(sizeof(wasm_halide_buffer_t) == 40, "wasm_halide_buffer_t");

    wdebug(0) << "\nhostbuf_to_wasmbuf:\n";
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

    wasm_halide_buffer_t *dst = (wasm_halide_buffer_t *) (base + dst_ptr);
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

    wasm_halide_buffer_t *src = (wasm_halide_buffer_t *) (base + src_ptr);

    internal_assert(src->device == 0);
    internal_assert(src->device_interface == 0);

    halide_buffer_t dst_tmp;
    dst_tmp.device = 0;
    dst_tmp.device_interface = 0;
    dst_tmp.host = nullptr; // src->host ? (base + src->host) : nullptr;
    dst_tmp.flags = src->flags;
    dst_tmp.type = src->type;
    dst_tmp.dimensions = src->dimensions;
    dst_tmp.dim = src->dim ? (halide_dimension_t *) (base + src->dim) : nullptr;
    dst_tmp.padding = 0;

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

    wasm_halide_buffer_t *src = (wasm_halide_buffer_t *) (base + src_ptr);
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
    dst->device_interface = 0;
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

    wasm_halide_buffer_t *dst = (wasm_halide_buffer_t *) (base + dst_ptr);
    internal_assert(src->device == 0);
    internal_assert(src->device_interface == 0);
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
    JITUserContext *jit_user_context = (JITUserContext *) context->GetAlignedPointerFromEmbedderData(kJitUserContext);
    internal_assert(jit_user_context);
    return jit_user_context;
}

void wasm_jit_halide_print_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    JITUserContext *jit_user_context = get_jit_user_context(context, args[0]);
    const int32_t str_address = args[1]->Int32Value(context).ToChecked();

    uint8_t *p = get_wasm_memory_base(context);
    const char *str = (const char *) p + str_address;

    if (jit_user_context && jit_user_context->handlers.custom_print != NULL) {
        (*jit_user_context->handlers.custom_print)(jit_user_context, str);
        debug(0) << str;
    } else {
        std::cout << str;
    }
}

void wasm_jit_halide_error_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    JITUserContext *jit_user_context = get_jit_user_context(context, args[0]);
    const int32_t str_address = args[1]->Int32Value(context).ToChecked();

    uint8_t *p = get_wasm_memory_base(context);
    const char *str = (const char *) p + str_address;

    if (jit_user_context && jit_user_context->handlers.custom_error != NULL) {
        (*jit_user_context->handlers.custom_error)(jit_user_context, str);
    } else {
        halide_runtime_error << str;
    }
}

void wasm_jit_halide_trace_helper_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
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
    event.func = (const char *) (base + func_name_ptr);
    event.value = value_ptr ? ((void *)(base + value_ptr)) : nullptr;
    event.coordinates = coordinates_ptr ? ((int32_t *)(base + coordinates_ptr)) : nullptr;
    event.trace_tag = (const char *) (base + trace_tag_ptr);
    event.type.code = (halide_type_code_t) type_code;
    event.type.bits = (uint8_t) type_bits;
    event.type.lanes = (uint16_t) type_lanes;
    event.event = (halide_trace_event_code_t) trace_code;
    event.parent_id = parent_id;
    event.value_index = value_index;
    event.dimensions = dimensions;

    int result = 0;
    if (jit_user_context && jit_user_context->handlers.custom_trace != NULL) {
        result = (*jit_user_context->handlers.custom_trace)(jit_user_context, &event);
    } else {
        debug(0) << "Dropping trace event due to lack of trace handler.\n";
    }

    args.GetReturnValue().Set(wrap_scalar(context, result));
}

// TODO: vector codegen can underead allocated buffers; we need to deliberately
// allocate extra and return a pointer partway in to avoid out-of-bounds access
// failures. https://github.com/halide/Halide/issues/3738
constexpr size_t kExtraMallocSlop = 32;

void wasm_jit_malloc_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();

    size_t size = args[0]->Int32Value(context).ToChecked() + kExtraMallocSlop;
    wasm32_ptr_t p = v8_WasmMemoryObject_malloc(context, size);
    if (p) p += kExtraMallocSlop;
    args.GetReturnValue().Set(wrap_scalar(context, p));
}

void wasm_jit_free_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    wasm32_ptr_t p = args[0]->Int32Value(context).ToChecked();
    if (p) p -= kExtraMallocSlop;
    v8_WasmMemoryObject_free(context, p);
}

void wasm_jit_abort_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    abort();
}

void wasm_jit_strlen_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const int32_t s = args[0]->Int32Value(context).ToChecked();

    uint8_t *base = get_wasm_memory_base(context);
    int32_t r = strlen((char *) base + s);

    args.GetReturnValue().Set(wrap_scalar(context, r));
}

void wasm_jit_write_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    internal_error << "WebAssembly JIT does not yet support the write() call.";
}

void wasm_jit_getenv_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const int32_t s = args[0]->Int32Value(context).ToChecked();

    uint8_t *base = get_wasm_memory_base(context);
    char *e = getenv((char *) base + s);

    // TODO: this string is leaked
    if (e) {
        wasm32_ptr_t r = v8_WasmMemoryObject_malloc(context, strlen(e) + 1);
        strcpy((char *) base + r, e);
        args.GetReturnValue().Set(wrap_scalar(context, r));
    } else {
        args.GetReturnValue().Set(wrap_scalar<wasm32_ptr_t>(context, 0));
    }
}

void wasm_jit_memcpy_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const int32_t dst = args[0]->Int32Value(context).ToChecked();
    const int32_t src = args[1]->Int32Value(context).ToChecked();
    const int32_t n = args[2]->Int32Value(context).ToChecked();

    uint8_t *base = get_wasm_memory_base(context);

    memcpy(base + dst, base + src, n);

    args.GetReturnValue().Set(wrap_scalar(context, dst));
}

void wasm_jit_fopen_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    internal_error << "WebAssembly JIT does not yet support the fopen() call.";
}

void wasm_jit_fileno_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    internal_error << "WebAssembly JIT does not yet support the fileno() call.";
}

void wasm_jit_fclose_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    internal_error << "WebAssembly JIT does not yet support the fclose() call.";
}

void wasm_jit_fwrite_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    internal_error << "WebAssembly JIT does not yet support the fwrite() call.";
}

void wasm_jit_memset_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const int32_t s = args[0]->Int32Value(context).ToChecked();
    const int32_t c = args[1]->Int32Value(context).ToChecked();
    const int32_t n = args[2]->Int32Value(context).ToChecked();

    uint8_t *base = get_wasm_memory_base(context);
    memset(base + s, c, n);

    args.GetReturnValue().Set(wrap_scalar(context, s));
}

void wasm_jit_memcmp_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const int32_t s1 = args[0]->Int32Value(context).ToChecked();
    const int32_t s2 = args[1]->Int32Value(context).ToChecked();
    const int32_t n = args[2]->Int32Value(context).ToChecked();

    uint8_t *base = get_wasm_memory_base(context);

    int r = memcmp(base + s1, base + s2, n);

    args.GetReturnValue().Set(wrap_scalar(context, r));
}

void wasm_jit___cxa_atexit_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    // nothing
}

template<typename T, T some_func(T) >
void wasm_jit_posix_math_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const T in = args[0]->NumberValue(context).ToChecked();
    const T out = some_func(in);

    args.GetReturnValue().Set(wrap_scalar(context, out));
}

template<typename T, T some_func(T, T) >
void wasm_jit_posix_math2_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    const T in1 = args[0]->NumberValue(context).ToChecked();
    const T in2 = args[1]->NumberValue(context).ToChecked();
    const T out = some_func(in1, in2);

    args.GetReturnValue().Set(wrap_scalar(context, out));
}

enum ExternWrapperFieldSlots {
    kTrampolineWrap,
    kArgTypesWrap
};

// Use a POD here so we can stuff it all into an ArrayBuffer to avoid having
// to worry about lifetime management
struct ExternArgType {
    halide_type_t type;
    bool is_void;
    bool is_buffer;
};

void v8_extern_wrapper(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> wrapper_data = args.Data()->ToObject(context).ToLocalChecked();

    Local<External> trampoline_wrap = Local<External>::Cast(wrapper_data->GetInternalField(kTrampolineWrap));
    Local<ArrayBuffer> arg_types_wrap = Local<ArrayBuffer>::Cast(wrapper_data->GetInternalField(kArgTypesWrap));

    using TrampolineFn = void (*)(void **);
    TrampolineFn trampoline = (TrampolineFn) trampoline_wrap->Value();

    size_t arg_types_len = (arg_types_wrap->ByteLength() / sizeof(ExternArgType)) - 1;
    const ExternArgType *arg_types = (const ExternArgType *) arg_types_wrap->GetContents().Data();
    const ExternArgType ret_type = *arg_types++;

    // There's wasted space here, but that's ok.
    std::vector<Halide::Runtime::Buffer<>> buffers(arg_types_len);
    std::vector<uint64_t> scalars(arg_types_len);
    std::vector<void *> trampoline_args(arg_types_len);

    for (size_t i = 0; i < arg_types_len; ++i) {
        if (arg_types[i].is_buffer) {
            const wasm32_ptr_t buf_ptr = args[i]->Int32Value(context).ToChecked();
            wasmbuf_to_hostbuf(context, buf_ptr, buffers[i]);
            trampoline_args[i] = buffers[i].raw_buffer();
        } else {
            dynamic_type_dispatch<ExtractAndStoreScalar>(arg_types[i].type, context, args[i], &scalars[i]);
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
        dynamic_type_dispatch<LoadAndReturnScalar>(ret_type.type, context, (void *) &ret_val, args.GetReturnValue());
    }

    // Progagate buffer data backwards. Note that for arbitrary extern functions,
    // we have no idea which buffers might be "input only", so we copy all data for all of them.
    for (size_t i = 0; i < arg_types_len; ++i) {
        if (arg_types[i].is_buffer) {
            const wasm32_ptr_t buf_ptr = args[i]->Int32Value(context).ToChecked();
            copy_hostbuf_to_existing_wasmbuf(context, buffers[i].raw_buffer(), buf_ptr);
        }
    }
}

bool should_skip_extern_symbol(const std::string &name) {
    static std::set<std::string> symbols = {
        "halide_print",
        "halide_error"
    };
    return symbols.count(name) > 0;
}

void add_extern_callbacks(const Local<Context> &context,
                          const JITExternMap &jit_externs,
                          const JITModule &trampolines,
                          Local<Object> &imports_dict) {
    Isolate *isolate = context->GetIsolate();
    Local<ObjectTemplate> extern_callback_template = ObjectTemplate::New(isolate);
    extern_callback_template->SetInternalFieldCount(4);
    for (const auto &it : jit_externs) {
        const auto &name = it.first;
        if (should_skip_extern_symbol(name)) {
            continue;
        }

        const auto &jit_extern = it.second;

        const auto &trampoline_symbol = trampolines.exports().find(name + kTrampolineSuffix);
        internal_assert(trampoline_symbol != trampolines.exports().end());

        const auto &sig = jit_extern.extern_c_function().signature();
        size_t arg_count = sig.arg_types().size();
        Local<ArrayBuffer> arg_types_wrap = ArrayBuffer::New(isolate, sizeof(ExternArgType) * (arg_count + 1));
        ExternArgType *arg_types = (ExternArgType *) arg_types_wrap->GetContents().Data();
        if (sig.is_void_return()) {
            // Type specified here will be ignored
            *arg_types++ = ExternArgType{ {halide_type_int, 0, 0}, true, false };
        } else {
            const Type &t = sig.ret_type();
            const bool is_buffer = (t == type_of<halide_buffer_t *>());
            user_assert(t.lanes() == 1) << "Halide Extern functions cannot return vector values.";
            user_assert(!is_buffer) << "Halide Extern functions cannot return halide_buffer_t.";
            // TODO: the assertion below can be removed once we are able to marshal int64 values across the barrier
            user_assert(!(t.is_handle() && !is_buffer)) << "Halide Extern functions cannot return arbitrary pointers as arguments.";
            // TODO: the assertion below can be removed once we are able to marshal int64 values across the barrier
            user_assert(!(t.is_int_or_uint() && t.bits() == 64)) << "Halide Extern functions cannot accept 64-bit values as arguments.";
            *arg_types++ = ExternArgType{ t, false, false };
        }
        for (size_t i = 0; i < arg_count; ++i) {
            const Type &t = sig.arg_types()[i];
            const bool is_buffer = (t == type_of<halide_buffer_t *>());
            user_assert(t.lanes() == 1) << "Halide Extern functions cannot accept vector values as arguments.";
            // TODO: the assertion below can be removed once we are able to marshal int64 values across the barrier
            user_assert(!(t.is_handle() && !is_buffer)) << "Halide Extern functions cannot accept arbitrary pointers as arguments.";
            // TODO: the assertion below can be removed once we are able to marshal int64 values across the barrier
            user_assert(!(t.is_int_or_uint() && t.bits() == 64)) << "Halide Extern functions cannot accept 64-bit values as arguments.";
            *arg_types++ = ExternArgType{ t, false, is_buffer };
        }

        Local<Object> wrapper_data = extern_callback_template->NewInstance(context).ToLocalChecked();
        Local<External> trampoline_wrap(External::New(isolate, const_cast<void *>(trampoline_symbol->second.address)));
        wrapper_data->SetInternalField(kTrampolineWrap, trampoline_wrap);
        wrapper_data->SetInternalField(kArgTypesWrap, arg_types_wrap);

        Local<v8::Function> f = FunctionTemplate::New(isolate, v8_extern_wrapper, wrapper_data)
            ->GetFunction(context).ToLocalChecked();

        (void) imports_dict->Set(context, String::NewFromUtf8(isolate, name.c_str()), f).ToChecked();
    }
}

std::vector<char> compile_to_wasm(const Module &module, const std::string &fn_name) {
    static std::mutex link_lock;
    std::lock_guard<std::mutex> lock(link_lock);

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> fn_module(compile_module_to_llvm_module(module, context));

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
    wdebug(0) << "Dumping obj_file to " << obj_file.pathname() << "\n";
#endif

    TemporaryFile wasm_output("", ".wasm");

    std::string lld_arg_strs[] = {
        "HalideJITLinker",
        // For debugging purposes:
        // "--verbose",
        // "-error-limit=0",
        // "--print-gc-sections",
        "--export=__data_end",
        "--export=__heap_base",
        "--allow-undefined",
        obj_file.pathname(),
        "--entry=" + fn_name,
        "-o",
        wasm_output.pathname()
    };

    constexpr int c = sizeof(lld_arg_strs)/sizeof(lld_arg_strs[0]);
    const char *lld_args[c];
    for (int i = 0; i < c; ++i) lld_args[i] = lld_arg_strs[i].c_str();

    std::string lld_errs_string;
    llvm::raw_string_ostream lld_errs(lld_errs_string);

    if (!lld::wasm::link(lld_args, /*CanExitEarly*/ false, lld_errs)) {
        internal_error << "lld::wasm::link failed: (" << lld_errs.str() << ")\n";
    }

#if WASM_DEBUG_LEVEL
    wasm_output.detach();
    wdebug(0) << "Dumping linked wasm to " << wasm_output.pathname() << "\n";
#endif

    return read_entire_file(wasm_output.pathname());
}


}  // namespace
}  // namespace Internal
}  // namespace Halide

#endif  // WITH_V8


namespace Halide {
namespace Internal {

struct WasmModuleContents {
    mutable RefCount ref_count;

    const Target target;
    const std::vector<Argument> arguments;
    JITExternMap jit_externs;
    std::vector<JITModule> extern_deps;
    JITModule trampolines;
    BDMalloc bdmalloc;

#ifdef WITH_V8
    v8::Isolate *isolate = nullptr;
    v8::ArrayBuffer::Allocator* array_buffer_allocator = nullptr;
    v8::Persistent<v8::Context> v8_context;
    v8::Persistent<v8::Function> v8_function;
#endif

    WasmModuleContents(
        const Module &module,
        const std::vector<Argument> &arguments,
        const std::string &fn_name,
        const JITExternMap &jit_externs,
        const std::vector<JITModule> &extern_deps
    );

    int run(const void **args);

    ~WasmModuleContents();
};

WasmModuleContents::WasmModuleContents(
    const Module &module,
    const std::vector<Argument> &arguments,
    const std::string &fn_name,
    const JITExternMap &jit_externs,
    const std::vector<JITModule> &extern_deps
) : target(module.target()),
    arguments(arguments),
    jit_externs(jit_externs),
    extern_deps(extern_deps),
    trampolines(JITModule::make_trampolines_module(get_host_target(), jit_externs, kTrampolineSuffix, extern_deps)) {

    wdebug(0) << "Compiling wasm function " << fn_name << "\n";

#if V8_API_VERSION < 75
    // V8 v7.4 works fine for non-SIMD work, but has various SIMD-related issues
    // that will make some of our self-tests fail (and thus probably cause user
    // code to be flaky as well); issue a warning if someone tries to test in this way.
    if (target.has_feature(Target::WasmSimd128)) {
        user_warning << "Versions of V8 prior to v7.5 may not work correctly with wasm_simd128 enabled.\n";
    }
#endif

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
            "--experimental-wasm-sat-f2i-conversions",  // +nontrapping-fptoint
            "--experimental_wasm_se",    // +sign-ext
            "--experimental_wasm_simd",  // +simd128
            // "--experimental_wasm_bulk_memory",

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
    Local<Context> context = Context::New(isolate, NULL, global);
    v8_context.Reset(isolate, context);

    Context::Scope context_scope(context);

    TryCatch try_catch(isolate);
    try_catch.SetCaptureMessage(true);
    try_catch.SetVerbose(true);

    auto fn_name_str = String::NewFromUtf8(isolate, fn_name.c_str());

    std::vector<char> final_wasm = compile_to_wasm(module, fn_name);

#if V8_API_VERSION < 74
    using WasmModuleObject = WasmCompiledModule;
#endif
    MaybeLocal<WasmModuleObject> maybe_compiled = WasmModuleObject::DeserializeOrCompile(isolate,
        /* serialized_module */ {nullptr, 0},
        /* wire_bytes */        {(const uint8_t *) final_wasm.data(), final_wasm.size()}
    );

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

    Local<Object> imports_dict = Object::New(isolate);

    const auto add_callback = [&](const char *name, FunctionCallback f) {
        // Skip any leading :: nonsense that we needed to add
        // to disambiguate (say) ::sin() from Halide::sin()
        while (*name == ':') name++;
        (void) imports_dict->Set(context, String::NewFromUtf8(isolate, name),
                                 FunctionTemplate::New(isolate, f)->GetFunction(context).ToLocalChecked()).ToChecked();
    };

#define ADD_CALLBACK(x) add_callback(#x, wasm_jit_##x##_callback);

    // Halide Runtime glue
    ADD_CALLBACK(halide_error);
    ADD_CALLBACK(halide_print);
    ADD_CALLBACK(halide_trace_helper);

    // libc-ish glue
    ADD_CALLBACK(__cxa_atexit)
    ADD_CALLBACK(abort)
    ADD_CALLBACK(fclose)
    ADD_CALLBACK(fileno)
    ADD_CALLBACK(fopen)
    ADD_CALLBACK(free)
    ADD_CALLBACK(fwrite)
    ADD_CALLBACK(getenv)
    ADD_CALLBACK(malloc)
    ADD_CALLBACK(memcmp)
    ADD_CALLBACK(memcpy)
    ADD_CALLBACK(memset)
    ADD_CALLBACK(strlen)
    ADD_CALLBACK(write)

#undef ADD_CALLBACK

#define ADD_POSIX_MATH(t, f)  add_callback(#f, wasm_jit_posix_math_callback<t, f>);
#define ADD_POSIX_MATH2(t, f) add_callback(#f, wasm_jit_posix_math2_callback<t, f>);

    // math glue
    ADD_POSIX_MATH(double,  ::acos)
    ADD_POSIX_MATH(double,  ::acosh)
    ADD_POSIX_MATH(double,  ::asin)
    ADD_POSIX_MATH(double,  ::asinh)
    ADD_POSIX_MATH(double,  ::atan)
    ADD_POSIX_MATH(double,  ::atanh)
    ADD_POSIX_MATH(double,  ::cos)
    ADD_POSIX_MATH(double,  ::cosh)
    ADD_POSIX_MATH(double,  ::exp)
    ADD_POSIX_MATH(double,  ::log)
    ADD_POSIX_MATH(double,  ::round)
    ADD_POSIX_MATH(double,  ::sin)
    ADD_POSIX_MATH(double,  ::sinh)
    ADD_POSIX_MATH(double,  ::tan)
    ADD_POSIX_MATH(double,  ::tanh)

    ADD_POSIX_MATH(float,   ::acosf)
    ADD_POSIX_MATH(float,   ::acoshf)
    ADD_POSIX_MATH(float,   ::asinf)
    ADD_POSIX_MATH(float,   ::asinhf)
    ADD_POSIX_MATH(float,   ::atanf)
    ADD_POSIX_MATH(float,   ::atanhf)
    ADD_POSIX_MATH(float,   ::cosf)
    ADD_POSIX_MATH(float,   ::coshf)
    ADD_POSIX_MATH(float,   ::expf)
    ADD_POSIX_MATH(float,   ::logf)
    ADD_POSIX_MATH(float,   ::roundf)
    ADD_POSIX_MATH(float,   ::sinf)
    ADD_POSIX_MATH(float,   ::sinhf)
    ADD_POSIX_MATH(float,   ::tanf)
    ADD_POSIX_MATH(float,   ::tanhf)

    ADD_POSIX_MATH2(float,  ::atan2f)
    ADD_POSIX_MATH2(double, ::atan2)
    ADD_POSIX_MATH2(float,  ::powf)
    ADD_POSIX_MATH2(double, ::pow)

#undef ADD_POSIX_MATH
#undef ADD_POSIX_MATH2

    add_extern_callbacks(context, jit_externs, trampolines, imports_dict);

    Local<Object> imports = Object::New(isolate);
    (void) imports->Set(context, String::NewFromUtf8(isolate, "env"), imports_dict).ToChecked();

    Local<Value> instance_args[2] = {compiled, imports};

    Local<Object> exports = context->Global()
        ->Get(context, String::NewFromUtf8(isolate, "WebAssembly")).ToLocalChecked().As<Object>()
        ->Get(context, String::NewFromUtf8(isolate, "Instance")).ToLocalChecked().As<Object>()
        ->CallAsConstructor(context, 2, instance_args).ToLocalChecked().As<Object>()
        ->Get(context, String::NewFromUtf8(isolate, "exports")).ToLocalChecked().As<Object>();

    Local<Value> function_value = exports->Get(fn_name_str);
    Local<v8::Function> function = Local<v8::Function>::Cast(function_value);
    internal_assert(!function.IsEmpty());
    internal_assert(!function->IsNullOrUndefined());
    v8_function.Reset(isolate, function);

    context->SetEmbedderData(kWasmMemoryObject, exports->Get(String::NewFromUtf8(isolate, "memory")).As<Object>());
    context->SetAlignedPointerInEmbedderData(kBDMallocPtr, &bdmalloc);
    context->SetEmbedderData(kHeapBase, exports->Get(String::NewFromUtf8(isolate, "__heap_base")).As<Object>());
    context->SetEmbedderData(kString_buffer, String::NewFromUtf8(isolate, "buffer"));
    context->SetEmbedderData(kString_grow, String::NewFromUtf8(isolate, "grow"));

    internal_assert(!try_catch.HasCaught());
#endif
}

int WasmModuleContents::run(const void **args) {
#ifdef WITH_V8
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
            halide_buffer_t *buf = (halide_buffer_t *) const_cast<void*>(arg_ptr);
            internal_assert(buf);
            wasm32_ptr_t wbuf = hostbuf_to_wasmbuf(context, buf);
            wbufs[i] = wbuf;
            js_args.push_back(wrap_scalar(context, wbuf));
        } else {
            if (arg.name == "__user_context") {
                js_args.push_back(wrap_scalar(context, kMagicJitUserContextValue));
                JITUserContext *jit_user_context = check_jit_user_context(*(JITUserContext **) const_cast<void *>(arg_ptr));
                context->SetAlignedPointerInEmbedderData(kJitUserContext, jit_user_context);
            } else {
                js_args.push_back(wrap_scalar(context, arg.type, arg_ptr));
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
                halide_buffer_t *buf = (halide_buffer_t *) const_cast<void*>(arg_ptr);
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
#endif

    internal_error;
    return -1;
}

WasmModuleContents::~WasmModuleContents() {
#ifdef WITH_V8
    if (isolate != nullptr) {
        // TODO: Do we have to do this explicitly, or does disposing the Isolate handle it?
        {
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolate_scope(isolate);

            v8_context.Reset();
            v8_function.Reset();
        }

        isolate->Dispose();
    }
    delete array_buffer_allocator;
#endif
}


template<>
RefCount &ref_count<WasmModuleContents>(const WasmModuleContents *p) {
    return p->ref_count;
}

template<>
void destroy<WasmModuleContents>(const WasmModuleContents *p) {
    delete p;
}

/*static*/
bool WasmModule::can_jit_target(const Target &target) {
    #if defined(WITH_V8) || defined(WITH_SPIDERMONKEY)
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
    const JITExternMap &jit_externs,
    const std::vector<JITModule> &extern_deps
) {
#if !defined(WITH_V8) && !defined(WITH_SPIDERMONKEY)
    user_error << "Cannot run JITted JavaScript without configuring a JavaScript engine.";
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
