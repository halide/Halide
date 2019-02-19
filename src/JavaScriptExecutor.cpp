#include "Error.h"
#include "CodeGen_JavaScript.h"
#include "ImageParam.h"
#include "JavaScriptExecutor.h"
#include "JITModule.h"
#include "Func.h"
#include "Target.h"

#include "runtime/HalideRuntime.h"
#include <sstream>
#include <valarray>
#include <vector>

namespace Halide {
namespace Internal {

// TODO: Filter math routines, runtime routines, etc.
std::map<std::string, Halide::JITExtern> filter_externs(const std::map<std::string, Halide::JITExtern> &externs) {
    std::map<std::string, Halide::JITExtern> result = externs;
    result.erase("halide_print");
    return result;
}

}  // namespace Internal
}  // namespace Halide

namespace {

using JITExternMap = std::map<std::string, Halide::JITExtern>;

}  // namespace

#if WITH_JAVASCRIPT_V8

#define buffer_t ???no_bad_wrong

#include "v8.h"
#include "libplatform/libplatform.h"

namespace Halide {
namespace Internal {

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


namespace JS_V8 {

using namespace v8;

// ------------------------------

template<typename T>
struct ExtractAndStoreScalar {
    void operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
        *(T *)slot = (T) val->NumberValue(context).ToChecked();
    }
};

template<>
inline void ExtractAndStoreScalar<void*>::operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
    Local<Object> wrapper_obj = val->ToObject(context).ToLocalChecked();
    Local<External> wrapped_handle = Local<External>::Cast(wrapper_obj->GetInternalField(0));
    *(uint64_t *)slot = (uint64_t) wrapped_handle->Value();
}

template<>
inline void ExtractAndStoreScalar<uint64_t>::operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
    user_error << "64-bit integer types are not supported with JavaScript.\n";
}

template<>
inline void ExtractAndStoreScalar<int64_t>::operator()(const Local<Context> &context, const Local<Value> &val, void *slot) {
    user_error << "64-bit integer types are not supported with JavaScript.\n";
}

// ------------------------------

template<typename T>
struct LoadAndReturnScalar {
    void operator()(const void *slot, ReturnValue<Value> val) {
        val.Set(*(const T *)slot);
    }
};

template<>
inline void LoadAndReturnScalar<void*>::operator()(const void *slot, ReturnValue<Value> val) {
    // TODO: I guess we could support this, but it seems unlikely to be of any use.
    user_error << "Returning handles is not supported in JavaScript.\n";
}

template<>
inline void LoadAndReturnScalar<uint64_t>::operator()(const void *slot, ReturnValue<Value> val) {
    user_error << "64-bit integer types are not supported with JavaScript.\n";
}

template<>
inline void LoadAndReturnScalar<int64_t>::operator()(const void *slot, ReturnValue<Value> val) {
    user_error << "64-bit integer types are not supported with JavaScript.\n";
}

// ------------------------------

template<typename T>
struct WrapScalar {
    Local<Value> operator()(Isolate *isolate, const void *val_ptr) {
        double val = *(const T *)(val_ptr);
        return Number::New(isolate, val);
    }
};

template<>
inline Local<Value> WrapScalar<void*>::operator()(Isolate *isolate, const void *val_ptr) {
    Local<ObjectTemplate> object_template = ObjectTemplate::New(isolate);
    object_template->SetInternalFieldCount(1);
    Local<Object> wrapper = object_template->NewInstance();
    Local<External> handle_wrap(External::New(isolate, *(void **)const_cast<void*>(val_ptr)));
    wrapper->SetInternalField(0, handle_wrap);
    return wrapper;
}

template<>
inline Local<Value> WrapScalar<uint64_t>::operator()(Isolate *isolate, const void *val_ptr) {
    user_error << "64-bit integer types are not supported with JavaScript.\n";
    return Local<Value>();
}

template<>
inline Local<Value> WrapScalar<int64_t>::operator()(Isolate *isolate, const void *val_ptr) {
    user_error << "64-bit integer types are not supported with JavaScript.\n";
    return Local<Value>();
}

// ------------------------------

Local<Value> wrap_scalar(Isolate *isolate, const Type &t, const void *val_ptr) {
    return dynamic_type_dispatch<WrapScalar>(t, isolate, val_ptr);
}

template<typename T>
Local<Value> wrap_scalar(Isolate *isolate, const T &val) {
    return WrapScalar<T>()(isolate, &val);
}

class HalideArrayBufferAllocator : public v8::ArrayBuffer::Allocator {
 public:
  virtual void* Allocate(size_t length) {
    void* data = AllocateUninitialized(length);
    return data == NULL ? data : memset(data, 0, length);
  }
  virtual void* AllocateUninitialized(size_t length) { return malloc(length); }
  virtual void Free(void* data, size_t) { free(data); }
};

JITUserContext *get_jit_user_context(Isolate *isolate, const Local<Value> &arg) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> user_context = arg->ToObject(context).ToLocalChecked();
    Local<External> handle_wrapper = Local<External>::Cast(user_context->GetInternalField(0));
    JITUserContext *jit_user_context = (JITUserContext *)handle_wrapper->Value();
    internal_assert(jit_user_context);
    return jit_user_context;
}

halide_buffer_t *extract_buffer_ptr(const Local<Object> &obj) {
    Local<External> buf_wrapper = Local<External>::Cast(obj->GetInternalField(0));
    auto *b = (halide_buffer_t *) buf_wrapper->Value();
    internal_assert(b);
    return b;
}

halide_buffer_t *extract_buffer_ptr(Isolate *isolate, const Local<Value> &val) {
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> obj = val->ToObject(context).ToLocalChecked();
    return extract_buffer_ptr(obj);
}

Local<Object> wrap_existing_buffer(Isolate *isolate, halide_buffer_t *buf) {
    EscapableHandleScope scope(isolate);

    Local<ObjectTemplate> buffer_template = ObjectTemplate::New(isolate);
    buffer_template->SetInternalFieldCount(4);

    Local<Object> buffer_wrapper = buffer_template->NewInstance();
    buffer_wrapper->SetInternalField(0, External::New(isolate, (void *) buf));  // ptr to halide_buffer_t
    buffer_wrapper->SetInternalField(1, External::New(isolate, nullptr));  // owned storage for the halide_buffer_t (none)
    buffer_wrapper->SetInternalField(2, External::New(isolate, nullptr));  // owned storage for host data (none)
    buffer_wrapper->SetInternalField(3, External::New(isolate, nullptr));  // owned storage for shape data (none)

    return scope.Escape(buffer_wrapper);
}

void _ensure_halide_buffer_shape_storage(Isolate *isolate, Local<Object> &buffer_wrapper, int dimensions) {
    HandleScope scope(isolate);

    halide_buffer_t *buf = extract_buffer_ptr(buffer_wrapper);
    internal_assert(buf);

    if (dimensions != buf->dimensions) {
        if (dimensions) {
            Local<ArrayBuffer> shape_storage = ArrayBuffer::New(isolate, sizeof(halide_dimension_t) * dimensions);
            halide_dimension_t *shape = (halide_dimension_t *) shape_storage->GetContents().Data();
            buf->dimensions = dimensions;
            buf->dim = shape;
            buffer_wrapper->SetInternalField(3, shape_storage);  // owned storage for shape data
        } else {
            buf->dimensions = 0;
            buf->dim = nullptr;
        }
    }
}

void _halide_buffer_create_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 0);

    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);

    Local<ObjectTemplate> buffer_template = ObjectTemplate::New(isolate);
    buffer_template->SetInternalFieldCount(4);

    Local<ArrayBuffer> buf_storage = ArrayBuffer::New(isolate, sizeof(halide_buffer_t));
    halide_buffer_t *buf = (halide_buffer_t *) buf_storage->GetContents().Data();
    memset(buf, 0, sizeof(*buf));

    Local<Object> buffer_wrapper = buffer_template->NewInstance();
    buffer_wrapper->SetInternalField(0, External::New(isolate, (void *) buf));  // ptr to halide_buffer_t
    buffer_wrapper->SetInternalField(1, buf_storage);                           // owned storage for the halide_buffer_t
    buffer_wrapper->SetInternalField(2, External::New(isolate, nullptr));       // owned storage for host data (none)
    buffer_wrapper->SetInternalField(3, External::New(isolate, nullptr));       // owned storage for shape data (none)

    args.GetReturnValue().Set(buffer_wrapper);
}

void _halide_buffer_get_dimensions_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 1);

    Isolate *isolate = args.GetIsolate();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    args.GetReturnValue().Set(wrap_scalar(isolate, buf->dimensions));
}

void _halide_buffer_get_host_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 1);

    Isolate *isolate = args.GetIsolate();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);

    if (buf->host == nullptr) {
        args.GetReturnValue().SetNull();
        return;
    }

    const size_t size_in_bytes = Buffer<>(*buf).size_in_bytes();
    Local<ArrayBuffer> array_buffer = ArrayBuffer::New(isolate, buf->host,
        size_in_bytes, ArrayBufferCreationMode::kExternalized);
    // Always return as uint8, regardless of our type
    args.GetReturnValue().Set(Uint8Array::New(array_buffer, 0, size_in_bytes));
}

void _halide_buffer_get_device_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 1);

    Isolate *isolate = args.GetIsolate();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    internal_assert(buf->device == 0);

    args.GetReturnValue().SetNull();
}

void _halide_buffer_get_device_interface_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 1);

    Isolate *isolate = args.GetIsolate();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    internal_assert(buf->device_interface == nullptr);

    args.GetReturnValue().SetNull();
}

void _halide_buffer_get_min_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    const int d = args[1]->Int32Value(context).ToChecked();
    internal_assert(d >= 0 && d < buf->dimensions);

    args.GetReturnValue().Set(wrap_scalar(isolate, buf->dim[d].min));
}

void _halide_buffer_get_max_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    const int d = args[1]->Int32Value(context).ToChecked();
    internal_assert(d >= 0 && d < buf->dimensions);
    args.GetReturnValue().Set(wrap_scalar(isolate, buf->dim[d].min + buf->dim[d].extent - 1));
}

void _halide_buffer_get_extent_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    const int d = args[1]->Int32Value(context).ToChecked();
    internal_assert(d >= 0 && d < buf->dimensions);

    args.GetReturnValue().Set(wrap_scalar(isolate, buf->dim[d].extent));
}

void _halide_buffer_get_stride_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    const int d = args[1]->Int32Value(context).ToChecked();
    internal_assert(d >= 0 && d < buf->dimensions);

    args.GetReturnValue().Set(wrap_scalar(isolate, buf->dim[d].stride));
}

void _halide_buffer_set_host_dirty_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    const bool v = args[1]->BooleanValue(context).ToChecked();
    buf->set_host_dirty(v);
    args.GetReturnValue().Set(wrap_scalar(isolate, 0));
}

void _halide_buffer_set_device_dirty_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    const bool v = args[1]->BooleanValue(context).ToChecked();
    buf->set_device_dirty(v);
    args.GetReturnValue().Set(wrap_scalar(isolate, 0));
}

void _halide_buffer_get_host_dirty_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 1);

    Isolate *isolate = args.GetIsolate();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    args.GetReturnValue().Set(wrap_scalar(isolate, buf->host_dirty()));
}

void _halide_buffer_get_device_dirty_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 1);

    Isolate *isolate = args.GetIsolate();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    args.GetReturnValue().Set(wrap_scalar(isolate, buf->device_dirty()));
}

void _halide_buffer_get_shape_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 1);
    // The only thing that downstream code does with this is pass it on
    // to _halide_buffer_init() for dst_shape, which we ignore; just return null.
    args.GetReturnValue().SetNull();
}

void _halide_buffer_is_bounds_query_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 1);

    Isolate *isolate = args.GetIsolate();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    bool r = (buf->host == NULL && buf->device == 0);
    args.GetReturnValue().Set(wrap_scalar(isolate, r));
}

void _halide_buffer_get_type_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 1);

    Isolate *isolate = args.GetIsolate();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    args.GetReturnValue().Set(wrap_scalar(isolate, buf->type.as_u32()));
}

void _halide_buffer_init_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 10);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    Local<Object> dst_wrap = args[0]->ToObject(context).ToLocalChecked();
    halide_buffer_t *dst = extract_buffer_ptr(dst_wrap);
    internal_assert(dst);

    // args[1] is dst_shape, which we ignore; it's necessary in native
    // code because that memory is managed elsewhere, but in JS, the
    // shape is managed by the buffer.

    void *host = nullptr;
    if (!args[2]->IsNull()) {
      Local<ArrayBufferView> host_array = Local<ArrayBufferView>::Cast(args[2]);
      internal_assert(host_array->HasBuffer());
      Local<ArrayBuffer> host_array_buffer = host_array->Buffer();
      host = host_array_buffer->GetContents().Data();
      internal_assert(host);
      host = (void *)((char *)host + host_array->ByteOffset());
    }

    const uint64_t device = 0;
    // This should be null (to handle 64-bits values), but allow a plain 0
    // as well, to simplify codegen.
    internal_assert(args[3]->IsNull() || args[3]->Int32Value(context).ToChecked() == 0);  // device

    halide_device_interface_t * const device_interface = nullptr;
    internal_assert(args[4]->IsNull());  // device_interface

    const int type_code = args[5]->Int32Value(context).ToChecked();
    internal_assert(type_code >= halide_type_int && type_code <= halide_type_handle);

    const int type_bits = args[6]->Int32Value(context).ToChecked();
    internal_assert(type_bits == 1 || type_bits == 8 || type_bits == 16 || type_bits == 32 || type_bits == 64);

    const int dimensions = args[7]->Int32Value(context).ToChecked();
    internal_assert(dimensions >= 0 && dimensions < 1024);  // not a hard limit, just a sanity check

    const int flags = args[9]->Int32Value(context).ToChecked();

    _ensure_halide_buffer_shape_storage(isolate, dst_wrap, dimensions);

    dst->host = (uint8_t *)host;
    dst->device = device;
    dst->device_interface = device_interface;
    dst->type.code = (halide_type_code_t) type_code;
    dst->type.bits = (uint8_t) type_bits;
    dst->type.lanes = 1;
    dst->flags = flags;

    dst->dimensions = dimensions;
    {
        Local<Array> shape_array = Local<Array>::Cast(args[8]);
        internal_assert((int) shape_array->Length() == dimensions * 4);
        for (int i = 0; i < dimensions; i++) {
          dst->dim[i].min = shape_array->Get(i*4 + 0)->Int32Value(context).ToChecked();
          dst->dim[i].extent = shape_array->Get(i*4 + 1)->Int32Value(context).ToChecked();
          dst->dim[i].stride = shape_array->Get(i*4 + 2)->Int32Value(context).ToChecked();
          dst->dim[i].flags = shape_array->Get(i*4 + 3)->Int32Value(context).ToChecked();
        }
    }

    // TODO: we never share shape storage, but always share host storage. Is this right?
    dst_wrap->SetInternalField(2, args[2]);  // owned storage for host data

    args.GetReturnValue().Set(args[0]);
}

void _halide_buffer_init_from_buffer_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 3);

    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);

    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> dst_wrap = args[0]->ToObject(context).ToLocalChecked();
    // args[1] is dst_shape, which we ignore; it's necessary in native
    // code because that memory is managed elsewhere, but in JS, the
    // shape is managed by the buffer.
    Local<Object> src_wrap = args[2]->ToObject(context).ToLocalChecked();

    halide_buffer_t *dst = extract_buffer_ptr(dst_wrap);
    halide_buffer_t *src = extract_buffer_ptr(src_wrap);

    dst->host = src->host;
    dst->device = src->device;
    dst->device_interface = src->device_interface;
    dst->type = src->type;
    dst->flags = src->flags;

    _ensure_halide_buffer_shape_storage(isolate, dst_wrap, src->dimensions);
    dst->dimensions = src->dimensions;
    for (int i = 0; i < dst->dimensions; i++) {
        dst->dim[i] = src->dim[i];
    }

    // TODO: we never share shape storage, but always share host storage. Is this right?
    dst_wrap->SetInternalField(2, src_wrap->GetInternalField(2));  // owned storage for host data

    args.GetReturnValue().Set(args[0]);
}

void _halide_buffer_crop_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 6);

    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);

    Local<Context> context = isolate->GetCurrentContext();

    JITUserContext *jit_user_context = get_jit_user_context(isolate, args[0]);
    Local<Object> dst_wrap = args[1]->ToObject(context).ToLocalChecked();
    // args[2] is dst_shape, which we ignore; it's necessary in native
    // code because that memory is managed elsewhere, but in JS, the
    // shape is managed by the buffer.
    Local<Object> src_wrap = args[3]->ToObject(context).ToLocalChecked();
    Local<Array> min_array = Local<Array>::Cast(args[4]);
    Local<Array> extent_array = Local<Array>::Cast(args[5]);

    halide_buffer_t *dst = extract_buffer_ptr(dst_wrap);
    halide_buffer_t *src = extract_buffer_ptr(src_wrap);

    dst->device = 0;
    dst->device_interface = 0;
    dst->host = src->host;
    dst->flags = src->flags;
    dst->type = src->type;
    _ensure_halide_buffer_shape_storage(isolate, dst_wrap, src->dimensions);

    internal_assert((int) min_array->Length() >= dst->dimensions);
    internal_assert((int) extent_array->Length() >= dst->dimensions);

    int64_t offset = 0;
    for (int i = 0; i < dst->dimensions; i++) {
        const int min = min_array->Get(i)->Int32Value(context).ToChecked();
        const int extent = extent_array->Get(i)->Int32Value(context).ToChecked();
        dst->dim[i] = src->dim[i];
        dst->dim[i].min = min;
        dst->dim[i].extent = extent;
        offset += (min - src->dim[i].min) * src->dim[i].stride;
    }
    if (dst->host) {
        dst->host += offset * src->type.bytes();
    }

    internal_assert(!src->device_interface);
    if (src->device_interface) {
        src->device_interface->device_crop(jit_user_context, src, dst);
    }

    // TODO: we never share shape storage, but always share host storage. Is this right?
    dst_wrap->SetInternalField(2, src_wrap->GetInternalField(2));  // owned storage for host data

    args.GetReturnValue().Set(dst_wrap);
}

void _halide_buffer_retire_crop_impl(JITUserContext *jit_user_context, halide_buffer_t *crop, halide_buffer_t *parent) {
    if (crop->device) {
        if (!parent->device) {
            // We have been given a device allocation by the extern
            // stage. It only represents the cropped region, so we
            // can't just give it to the parent.
            if (crop->device_dirty()) {
                crop->device_interface->copy_to_host(jit_user_context, crop);
            }
            crop->device_interface->device_free(jit_user_context, crop);
        } else {
            // We are a crop of an existing device allocation.
            if (crop->device_dirty()) {
                parent->set_device_dirty();
            }
            crop->device_interface->device_release_crop(jit_user_context, crop);
        }
    }
    if (crop->host_dirty()) {
        parent->set_host_dirty();
    }
}

void _halide_buffer_retire_crop_after_extern_stage_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    JITUserContext *jit_user_context = get_jit_user_context(isolate, args[0]);
    Local<Array> buffer_array = Local<Array>::Cast(args[2]);
    internal_assert(buffer_array->Length() == 2);
    halide_buffer_t *crop = extract_buffer_ptr(buffer_array->Get(0)->ToObject(context).ToLocalChecked());
    halide_buffer_t *parent = extract_buffer_ptr(buffer_array->Get(1)->ToObject(context).ToLocalChecked());
    _halide_buffer_retire_crop_impl(jit_user_context, crop, parent);
    args.GetReturnValue().Set(0);
}

void _halide_buffer_retire_crops_after_extern_stage_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    JITUserContext *jit_user_context = get_jit_user_context(isolate, args[0]);
    Local<Array> buffer_array = Local<Array>::Cast(args[2]);
    size_t index = 0;
    while (index < buffer_array->Length()) {
        Local<Object> crop = buffer_array->Get(index++)->ToObject(context).ToLocalChecked();
        if (crop->IsNull()) break;

        Local<Object> parent = buffer_array->Get(index++)->ToObject(context).ToLocalChecked();
        if (parent->IsNull()) break;

        _halide_buffer_retire_crop_impl(jit_user_context, extract_buffer_ptr(crop), extract_buffer_ptr(parent));
    }
    args.GetReturnValue().Set(0);
}

void _halide_buffer_set_bounds_callback(const FunctionCallbackInfo<Value>& args) {
    internal_assert(args.Length() == 4);

    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    halide_buffer_t *buf = extract_buffer_ptr(isolate, args[0]);
    const int d = args[1]->Int32Value(context).ToChecked();
    internal_assert(d >= 0 && d < buf->dimensions);
    const int min = args[2]->Int32Value(context).ToChecked();
    const int extent = args[3]->Int32Value(context).ToChecked();
    buf->dim[d].min = min;
    buf->dim[d].extent = extent;
    args.GetReturnValue().Set(args[0]);
}

void halide_print_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Value> arg = args[1];
    String::Utf8Value value(isolate, arg);
    // Turns out to be convenient to get debug output in some cases where the
    // user_context is not setup.
    if (args[0]->IsNull()) {
        debug(0) << "Bad user_context to halide_print: " << *value;
        return;
    }

    JITUserContext *jit_user_context = get_jit_user_context(isolate, args[0]);
    if (jit_user_context->handlers.custom_print != NULL) {
        (*jit_user_context->handlers.custom_print)(jit_user_context, *value);
        debug(0) << *value;
    } else {
        // TODO: Figure out a better way to send output...
        debug(0) << *value;
    }
}

void halide_error_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    internal_assert(args.Length() == 2);

    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Value> arg = args[1];
    String::Utf8Value value(isolate, arg);

    // Turns out to be convenient to get debug output in some cases where the
    // user_context is not setup.
    if (args[0]->IsNull()) {
        halide_runtime_error << "Bad user_context to halide_error: " << *value;
        return;
    }

    JITUserContext *jit_user_context = get_jit_user_context(isolate, args[0]);
    if (jit_user_context->handlers.custom_error != NULL) {
        (*jit_user_context->handlers.custom_error)(jit_user_context, *value);
    } else {
        halide_runtime_error << *value;
    }
}

std::unique_ptr<uint8_t> make_trace_value(
    const Local<Context> &context,
    const Local<Object> &val_array,
    halide_type_code_t type_code,
    int32_t bits,
    int32_t vector_width
) {
    if (val_array->IsUndefined() || val_array->IsNull()) {
        return std::unique_ptr<uint8_t>();
    }
    const Type type(type_code, bits, 1);
    const size_t elem_size = type.bytes();
    const size_t total_size = elem_size * vector_width;
    std::unique_ptr<uint8_t> result(new uint8_t(total_size));
    uint8_t *ptr = result.get();
    for (int32_t i = 0; i < vector_width; i++) {
        dynamic_type_dispatch<ExtractAndStoreScalar>(type, context, val_array->Get(i), (void *) ptr);
        ptr += elem_size;
    }

    return result;
}

void halide_trace_helper_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    internal_assert(args.Length() == 12);
    Isolate *isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();
    HandleScope scope(isolate);

    Local<Object> user_context = args[0]->ToObject(context).ToLocalChecked();
    Local<External> handle_wrapper = Local<External>::Cast(user_context->GetInternalField(0));
    JITUserContext *jit_user_context = (JITUserContext *)handle_wrapper->Value();
    internal_assert(jit_user_context);

    String::Utf8Value func_name(isolate, args[1]->ToObject(context).ToLocalChecked());

    const int type_code = args[4]->Int32Value(context).ToChecked();
    const int type_bits = args[5]->Int32Value(context).ToChecked();
    const int type_lanes = args[6]->Int32Value(context).ToChecked();
    const int trace_code = args[7]->Int32Value(context).ToChecked();
    const int parent_id = args[8]->Int32Value(context).ToChecked();
    const int value_index = args[9]->Int32Value(context).ToChecked();
    const int dimensions = args[10]->Int32Value(context).ToChecked();
    String::Utf8Value trace_tag(isolate, args[11]->ToObject(context).ToLocalChecked());

    internal_assert(dimensions >= 0 && dimensions < 1024);  // not a hard limit, just a sanity check

    void *value = nullptr;
    std::unique_ptr<uint8_t> value_storage;
    if (!args[2]->IsNull()) {
        Local<Object> value_obj = args[2]->ToObject(context).ToLocalChecked();
        value_storage = make_trace_value(context, value_obj, (halide_type_code_t) type_code, type_bits, type_lanes);
        value = (void *) value_storage.get();
    }

    int32_t *coordinates = nullptr;
    std::vector<int32_t> coordinates_storage(dimensions);
    if (!args[3]->IsNull()) {
        Local<Array> coordinates_array = Local<Array>::Cast(args[3]);
        internal_assert((int) coordinates_array->Length() == dimensions);
        for (int i = 0; i < dimensions; ++i) {
            coordinates_storage[i] = coordinates_array->Get(i)->Int32Value(context).ToChecked();
        }
        coordinates = coordinates_storage.data();
    }

    halide_trace_event_t event;
    event.func = *func_name;
    event.value = value;
    event.coordinates = coordinates;
    event.trace_tag = *trace_tag;
    event.type.code = (halide_type_code_t) type_code;
    event.type.bits = (uint8_t) type_bits;
    event.type.lanes = (uint16_t) type_lanes;
    event.event = (halide_trace_event_code_t) trace_code;
    event.parent_id = parent_id;
    event.value_index = value_index;
    event.dimensions = dimensions;

    int result = 0;
    if (jit_user_context->handlers.custom_trace != NULL) {
        result = (*jit_user_context->handlers.custom_trace)(jit_user_context, &event);
    } else {
        // TODO: Should we try to call halide_default_trace here?
        debug(0) << "Dropping trace event due to lack of trace handler.\n";
    }

    args.GetReturnValue().Set(wrap_scalar(isolate, result));
}

int compile_function(Isolate *isolate, Local<Context> &context,
                     const std::string &fn_name, const std::string &source,
                     Local<v8::Function> &result) {
    // Create a string containing the JavaScript source code.
    Local<String> source_v8 = String::NewFromUtf8(isolate, source.c_str());

    TryCatch try_catch(isolate);
    try_catch.SetCaptureMessage(true);

    // Compile the source code.
    MaybeLocal<Script> compiled_script = Script::Compile(context, source_v8);
    if (compiled_script.IsEmpty()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        internal_error << "Error compiling JavaScript: " << *error << "\n";
        return -1;
    }
    MaybeLocal<Value> run_result = compiled_script.ToLocalChecked()->Run(context);
    if (run_result.IsEmpty()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        internal_error << "Error running JavaScript: " << *error << "\n";
        return -1;
    }

    result = Local<v8::Function>::Cast(context->Global()->Get(String::NewFromUtf8(isolate, fn_name.c_str())));
    return 0;
}

void js_value_to_uint64_slot(const Local<Context> &context, const Halide::Type &type, const Local<Value> &val, uint64_t *slot) {
    dynamic_type_dispatch<ExtractAndStoreScalar>(type, context, val, (void *) slot);
}

void v8_extern_wrapper(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Context> context = isolate->GetCurrentContext();
    Local<Object> wrapper_data = args.Data()->ToObject(context).ToLocalChecked();

    Local<String> extern_name = Local<String>::Cast(wrapper_data->GetInternalField(0));
    Local<External> jit_extern_wrap = Local<External>::Cast(wrapper_data->GetInternalField(1));
    Local<External> trampoline_wrap = Local<External>::Cast(wrapper_data->GetInternalField(2));

    const JITExternMap *jit_externs = (const JITExternMap *) jit_extern_wrap->Value();
    void (*trampoline)(void **) = (void (*)(void **))trampoline_wrap->Value();

    String::Utf8Value str_val(isolate, extern_name);
    auto iter = jit_externs->find(*str_val);
    internal_assert(iter != jit_externs->end()) << "jit_extern " << *str_val << " not found in map.";

    const auto &arg_types = iter->second.extern_c_function().signature().arg_types();
    // Note that this allocates wasted space for buffer args too, but that's ok.
    std::vector<uint64_t> scalar_args_storage(arg_types.size());

    size_t args_index = 0;
    std::vector<void *> trampoline_args(arg_types.size());
    for (const Type &arg_type : arg_types) {
        const auto &arg = args[args_index];
        if (arg_type == type_of<halide_buffer_t *>()) {
            halide_buffer_t *buf = extract_buffer_ptr(isolate, arg);
            internal_assert(buf);
            trampoline_args[args_index] = (void *) buf;
        } else {
            auto *slot = &scalar_args_storage[args_index];
            js_value_to_uint64_slot(context, arg_type, arg, slot);
            trampoline_args[args_index] = (void *) slot;
        }
        args_index++;
    }

    uint64_t ret_val = 0;
    const bool has_retval = !iter->second.extern_c_function().signature().is_void_return();
    if (has_retval) {
        trampoline_args.push_back(&ret_val);
    }
    (*trampoline)(trampoline_args.data());

    if (has_retval) {
        dynamic_type_dispatch<LoadAndReturnScalar>(iter->second.extern_c_function().signature().ret_type(), (void *) &ret_val, args.GetReturnValue());
    }
}

Local<ObjectTemplate> make_global_template(Isolate *isolate) {
    // Create a template for the global object where we set the
    // built-in global functions.
    Local<ObjectTemplate> global = ObjectTemplate::New(isolate);

    #define ADD_CALLBACK_FUNCTION(n) \
        do { global->Set(String::NewFromUtf8(isolate, #n), FunctionTemplate::New(isolate, n##_callback)); } while (0)

    ADD_CALLBACK_FUNCTION(halide_error);
    ADD_CALLBACK_FUNCTION(halide_print);
    ADD_CALLBACK_FUNCTION(halide_trace_helper);

    ADD_CALLBACK_FUNCTION(_halide_buffer_create);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_dimensions);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_host);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_device);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_device_interface);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_min);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_max);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_extent);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_stride);
    ADD_CALLBACK_FUNCTION(_halide_buffer_set_host_dirty);
    ADD_CALLBACK_FUNCTION(_halide_buffer_set_device_dirty);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_host_dirty);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_device_dirty);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_shape);
    ADD_CALLBACK_FUNCTION(_halide_buffer_is_bounds_query);
    ADD_CALLBACK_FUNCTION(_halide_buffer_get_type);
    ADD_CALLBACK_FUNCTION(_halide_buffer_init);
    ADD_CALLBACK_FUNCTION(_halide_buffer_init_from_buffer);
    ADD_CALLBACK_FUNCTION(_halide_buffer_crop);
    ADD_CALLBACK_FUNCTION(_halide_buffer_retire_crop_after_extern_stage);
    ADD_CALLBACK_FUNCTION(_halide_buffer_retire_crops_after_extern_stage);
    ADD_CALLBACK_FUNCTION(_halide_buffer_set_bounds);

    #undef ADD_CALLBACK_FUNCTION

    return global;
}

void add_extern_callbacks(Isolate *isolate,
                          Local<Context> &context,
                          const JITExternMap &externs,
                          JITModule trampolines) {
    for (const std::pair<std::string, JITExtern> &jit_extern : externs) {
        Local<ObjectTemplate> extern_callback_template = ObjectTemplate::New(isolate);
        extern_callback_template->SetInternalFieldCount(3);

        Local<Object> wrapper_data = extern_callback_template->NewInstance();
        Local<External> jit_externs_wrap(External::New(isolate, (void *)&externs));
        Local<External> trampoline_wrap(External::New(isolate,
            const_cast<void *>(trampolines.exports().find(jit_extern.first + "_js_trampoline")->second.address)));
        wrapper_data->SetInternalField(0, String::NewFromUtf8(isolate, jit_extern.first.c_str()));
        wrapper_data->SetInternalField(1, jit_externs_wrap);
        wrapper_data->SetInternalField(2, trampoline_wrap);
        Local<v8::Function> f = FunctionTemplate::New(isolate, v8_extern_wrapper, wrapper_data)->GetFunction();
        context->Global()->Set(String::NewFromUtf8(isolate, jit_extern.first.c_str()), f);
    }
  }

} // namespace JS_V8;

int compile_javascript_v8(const std::string &source, const std::string &fn_name,
                          const JITExternMap &externs,
                          JITModule trampolines,
                          JS_V8::Isolate *&isolate,
                          JS_V8::Persistent<JS_V8::Context> &context_holder,
                          JS_V8::Persistent<JS_V8::Function> &function_holder) {
    using namespace JS_V8;

    debug(1) << "Compiling JavaScript function " << fn_name << "\n";
    // TODO: thread safety.
    static std::unique_ptr<ArrayBuffer::Allocator> array_buffer_allocator(new HalideArrayBufferAllocator());
    // Use this approach to ensure V8 is inited only once, if multiple threads are in use
    static bool inited = []() -> bool {
        // Initialize V8.
        V8::InitializeICU();
        Platform* platform = platform::CreateDefaultPlatform();
        V8::InitializePlatform(platform);
        V8::Initialize();
        return true;
    }();
    internal_assert(inited);

    Isolate::CreateParams isolate_params;
    isolate_params.array_buffer_allocator = array_buffer_allocator.get();
    // Create a new Isolate and make it the current one.
    isolate = Isolate::New(isolate_params);

    Isolate::Scope isolate_scope(isolate);

    // Create a stack-allocated handle scope.
    HandleScope handle_scope(isolate);

    Local<Context> context = Context::New(isolate, NULL, make_global_template(isolate));
    // Create a new context.
    context_holder.Reset(isolate, context);

    // Enter the context for compiling and running the hello world script.
    Context::Scope context_scope(context);

    add_extern_callbacks(isolate, context, externs, trampolines);

    TryCatch try_catch(isolate);
    try_catch.SetCaptureMessage(true);

    Local<v8::Function> function;
    if (compile_function(isolate, context, fn_name, source, function) != 0) {
        return -1;
    }
    function_holder.Reset(isolate, function);
    return 0;
}

int run_javascript_v8(std::vector<std::pair<Argument, const void *>> args,
                      v8::Isolate *&isolate, v8::Persistent<v8::Context> &context_holder,
                      v8::Persistent<v8::Function> &function_holder) {
    using namespace JS_V8;

    Isolate::Scope isolate_scope(isolate);

    // Create a stack-allocated handle scope.
    HandleScope handle_scope(isolate);

    Local<Context> context = Local<Context>::New(isolate, context_holder);
    // Enter the context for compiling and running the hello world script.
    Context::Scope context_scope(context);

    TryCatch try_catch(isolate);
    try_catch.SetCaptureMessage(true);

    std::vector<v8::Handle<Value>> js_args;
    for (size_t i = 0; i < args.size(); i++) {
        Argument &arg = args[i].first;
        if (arg.is_buffer()) {
            halide_buffer_t *buf = (halide_buffer_t *) const_cast<void*>(args[i].second);
            js_args.push_back(wrap_existing_buffer(isolate, buf));
        } else {
            js_args.push_back(wrap_scalar(isolate, arg.type, args[i].second));
        }
    }

    Local<v8::Function> function = Local<v8::Function>::New(isolate, function_holder);
    MaybeLocal<Value> result = function->Call(context, context->Global(), js_args.size(), js_args.data());

    if (result.IsEmpty()) {
        String::Utf8Value error(isolate, try_catch.Exception());
        String::Utf8Value message(isolate, try_catch.Message()->GetSourceLine(context).ToLocalChecked());

        internal_error << "Error running JavaScript: " << *error << " | Line: " << *message << "\n";
        return -1;
    }

    return result.ToLocalChecked()->Int32Value(context).ToChecked();
}

}} // close Halide::Internal namespace

#endif

#if WITH_JAVASCRIPT_SPIDERMONKEY

/*
    This code is kept in place (though disabled by default) because it
    is severely out of date and is unlikely to even compile, much less
    work correctly; it needs rewriting from (mostly) scratch
    to match the V8-based glue. It is kept in place to emphasize that the support
    in this file is not intended to be V8-only; this code should be updated
    if this branch is ever to land.
*/
#error "JS-SpiderMonkey support not compile, please see comments"

// SpiderMonkey headers do not compile with -Werror and -Wall
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"

// If SpiderMonkey was compiled in debug mode, DEBUG must be defined before including
// its headers or else there will be link errors. E.g.:
//     Undefined symbols for architecture x86_64:
//     "JSAutoCompartment::JSAutoCompartment(JSContext*, JSObject*)", referenced from:
//     (...<something in JavaScripExecutor.cpp>...)
// This is due to SpiderMonkey changing the prototypes of APIs based on whether DEBUG
// is defined or not.
//#define DEBUG
#include "jsapi.h"
#include "jsfriendapi.h"
#include "js/Conversions.h"
#include "js/Initialization.h"

#pragma clang diagnostic pop

namespace Halide { namespace Internal {

namespace JS_SpiderMonkey {

using namespace JS;

void dump_object(const char *label, JSContext *context, HandleObject obj) {
  Rooted<IdVector> ids(context, IdVector(context));
    if (JS_Enumerate(context, obj, &ids)) {
        debug(0) << label << ": getting ids failed.\n";
    } else {
        if (ids.length() == 0) {
            debug(0) << label << ": object is empty.\n";
        }
        for (uint32_t i = 0; i < ids.length(); i++) {
            RootedId cur_rooted_id(context, ids[i]);
            RootedValue id_val(context);
            JS_IdToValue(context, ids[i], &id_val);

            RootedValue val(context);
            JS_GetPropertyById(context, obj, cur_rooted_id, &val);

            RootedString id_str(context, JS_ValueToSource(context, id_val));
            RootedString val_str(context, JS_ValueToSource(context, val));

            char *id_utf8 = JS_EncodeStringToUTF8(context, id_str);
            char *val_utf8 = JS_EncodeStringToUTF8(context, val_str);

            // debug(0) << label << ": id is " << id_utf8 << " val is " << val_utf8 << "\n";

            JS_free(context, val_utf8);
            JS_free(context, id_utf8);
        }
    }
}

// The class of the global object.
static JSClass global_class = {
    "global",
    JSCLASS_GLOBAL_FLAGS,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL,
    JS_GlobalObjectTraceHook,
    { }
};

// The error reporter callback.
void reportError(JSContext *cx, const char *message, JSErrorReport *report) {
    const char *filename = report->filename;
    if (filename == NULL) {
        filename = "<no filename>";
    }
    internal_error << "Error running JavaScript: " << message << " | File: " << filename << " Line: " << report->lineno << "\n";
}

ExternalArrayType halide_type_to_external_array_type(const Type &t) {
  if (t.is_uint()) {
      switch (t.bits()) {
          case 1:
          case 8:
              return kExternalUint8Array;
              break;
          case 16:
              return kExternalUint16Array;
              break;
          case 32:
              return kExternalUint32Array;
              break;
          default:
              internal_error << "Unsupported bit size.\n";
              return kExternalUint8Array;
              break;
      }
  } else if (t.is_int()) {
      switch (t.bits()) {
           case 8:
               return kExternalInt8Array;
               break;
           case 16:
               return kExternalInt16Array;
               break;
           case 32:
               return kExternalInt32Array;
               break;
           default:
               internal_error << "Unsupported bit size.\n";
               return kExternalInt8Array;
               break;
       }
   } else   if (t.is_float()) {
       switch (t.bits()) {
           case 32:
               return kExternalFloat32Array;
               break;
           case 64:
               return kExternalFloat64Array;
               break;
           default:
               internal_error << "Unsupported bit size.\n";
               return kExternalFloat32Array;
               break;
       }
   }

   internal_error << "Unsupported buffer type.\n";
   return kExternalUint8Array;
}

JSObject *make_array_of_type(JSContext *context, HandleObject array_buffer, ExternalArrayType element_type) {
    internal_assert(JS_IsArrayBufferObject(&*array_buffer)) << "Passed array buffer is not an array buffer object (SpiderMonkey).\n";
    switch (element_type) {
      case kExternalInt8Array:
        //        debug(0) << "Making Int8Array.\n";
        return JS_NewInt8ArrayWithBuffer(context, array_buffer, 0, -1);
        break;
      case kExternalUint8Array:
        //        debug(0) << "Making Uint8Array.\n";
        return JS_NewUint8ArrayWithBuffer(context, array_buffer, 0, -1);
        break;
      case kExternalInt16Array:
        //        debug(0) << "Making Int16Array.\n";
        return JS_NewInt16ArrayWithBuffer(context, array_buffer, 0, -1);
        break;
      case kExternalUint16Array:
        //        debug(0) << "Making Uint16Array.\n";
        return JS_NewUint16ArrayWithBuffer(context, array_buffer, 0, -1);
        break;
      case kExternalInt32Array:
        //        debug(0) << "Making Int32Array.\n";
        return JS_NewInt32ArrayWithBuffer(context, array_buffer, 0, -1);
        break;
      case kExternalUint32Array:
        //        debug(0) << "Making Uint32Array.\n";
        return JS_NewUint32ArrayWithBuffer(context, array_buffer, 0, -1);
        break;
      case kExternalFloat32Array:
        //        debug(0) << "Making Float32Array.\n";
        return JS_NewFloat32ArrayWithBuffer(context, array_buffer, 0, -1);
        break;
     case kExternalFloat64Array:
       //        debug(0) << "Making Float64Array.\n";
        return JS_NewFloat64ArrayWithBuffer(context, array_buffer, 0, -1);
        break;
     default:
        internal_error << "Unknown array type.\n";
        break;
    }
    return NULL;
}

bool dev_getter(JSContext *cx, unsigned argc, JS::Value *vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    //    debug(0) << "Dev getter this is " << &args.thisv().toObject() << " argv[0] is " << &args[0].toObject() << " argv[1] is " << &args[1].toObject()  << "\n";

    buffer_t *buf = (buffer_t *)JS_GetPrivate(&args.thisv().toObject());
    // TODO: Figure out how to do this via an object.
    args.rval().setInt32(buf->dev);
    return true;
}

bool dev_setter(JSContext *cx, unsigned argc, JS::Value *vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    //    debug(0) << "Dev setter this is " << &args.thisv().toObject() << " argv[0] is " << &args[0].toObject() << " argv[1] is " << &args[1].toObject()  << "\n";
    //    buffer_t *buf = (buffer_t *)JS_GetPrivate(&args.thisv().toObject());
    // TODO: Figure out how to do this via an object.
    args.rval().setInt32(0);
    return true;
}

bool elem_size_getter(JSContext *cx, unsigned argc, JS::Value *vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    //    debug(0) << "Elem size getter this is " << &args.thisv().toObject() << " argv[0] is " << &args[0].toObject() << " argv[1] is " << &args[1].toObject()  << "\n";
    buffer_t *buf = (buffer_t *)JS_GetPrivate(&args.thisv().toObject());
    args.rval().setInt32(buf->elem_size);
    //    debug(0) << "buf is " << buf << " elem size is " << buf->elem_size << "\n";
    return true;
}

bool elem_size_setter(JSContext *cx, unsigned argc, JS::Value *vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    //    debug(0) << "Elem size setter this is " << &args.thisv().toObject() << " argv[0] is " << &args[0].toObject() << " argv[1] is " << &args[1].toObject()  << "\n";
    buffer_t *buf = (buffer_t *)JS_GetPrivate(&args.thisv().toObject());
    //    debug(0) << "buf is " << buf << " elem size is " << buf->elem_size << "\n";
    buf->elem_size = args[1].toInt32();
    //    debug(0) << "buf is " << buf << " elem size is " << buf->elem_size << "\n";
    args.rval().setInt32(buf->elem_size);
    return true;
}

static JSClass buffer_t_class = {
    "buffer_t",
    JSCLASS_HAS_PRIVATE,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL,
    NULL,
  { }
};

Value make_buffer_t(JSContext *context, struct buffer_t *buf, ExternalArrayType element_type) {
    Value result;
    RootedObject buffer(context, JS_NewObject(context, &buffer_t_class));
    JS_SetPrivate(buffer, buf);

    //    debug(0) << "Making host buffer for type " << (int)element_type << " on " << buf << " object is " << &*buffer << " of length " << buffer_total_size(buf) * buf->elem_size << "\n";
    if (buf->host != NULL) {
        // SpiderMonkey insists on being able to steal the low bit of
        // a pointr in all circumstances apparently and there is an
        // assert that fires if the contents pointer of an array is
        // odd. Of course this means one cannot make a data backed
        // array directly on an oddly aligned byte pointer. If
        // necessary, presumably one makes an aligned ArrayBuffer then
        // a view on that to adjust the offset. (The docs say the
        // pointer passed in must be valid to pass to free, hence I
        // guess they can claim stealing the bit is legal. The API
        // docs for both SpiderMonkey and V8 make Halide's bare
        // minimum of documentation look like a thing of beauty...)
        //
        // Anyway, Halide passes "1" as a pointer for input buffers
        // that are not used when infer_bounds is called. This hack
        // fixes that up.
        uint8_t *host_ptr = buf->host;
        if (host_ptr == (uint8_t *)1) {
            host_ptr = (uint8_t *)2;
        }

        RootedObject host_buffer(context, JS_NewArrayBufferWithContents(context, buffer_total_size(buf) * buf->elem_size, host_ptr));
        RootedObject host_array(context, make_array_of_type(context, host_buffer, element_type));
        JS_DefineProperty(context, buffer, "host", host_array, JSPROP_READONLY | JSPROP_ENUMERATE);
    } else {
        RootedValue temp_null(context, NullValue());
        JS_DefineProperty(context, buffer, "host", temp_null, JSPROP_READONLY | JSPROP_ENUMERATE);
    }

    //    debug(0) << "Making min buffer of length " << sizeof(buf->min) << "\n";
    RootedObject min_buffer(context, JS_NewArrayBufferWithContents(context, sizeof(buf->min), &buf->min[0]));
    RootedObject min_array(context, JS_NewInt32ArrayWithBuffer(context, min_buffer, 0, -1));
    JS_DefineProperty(context, buffer, "min", min_array, JSPROP_READONLY | JSPROP_ENUMERATE);

    RootedObject stride_buffer(context, JS_NewArrayBufferWithContents(context, sizeof(buf->stride), &buf->stride[0]));
    RootedObject stride_array(context, JS_NewInt32ArrayWithBuffer(context, stride_buffer, 0, -1));
    JS_DefineProperty(context, buffer, "stride", stride_array, JSPROP_READONLY | JSPROP_ENUMERATE);

    RootedObject extent_buffer(context, JS_NewArrayBufferWithContents(context, sizeof(buf->extent), &buf->extent[0]));
    RootedObject extent_array(context, JS_NewInt32ArrayWithBuffer(context, extent_buffer, 0, -1));
    JS_DefineProperty(context, buffer, "extent", extent_array, JSPROP_READONLY | JSPROP_ENUMERATE);

    JS_DefineProperty(context, buffer, "dev", JS::UndefinedHandleValue, JSPROP_ENUMERATE | JSPROP_SHARED, dev_getter, dev_setter);
    JS_DefineProperty(context, buffer, "elem_size", JS::UndefinedHandleValue, JSPROP_ENUMERATE | JSPROP_SHARED, elem_size_getter, elem_size_setter);

    result.setObject(*buffer);
    return result;
}

bool disconnect_array_buffer(JSContext *context, HandleValue buffer, const char *name) {
    RootedObject buffer_obj(context, &buffer.toObject());
    RootedValue typed_array_val(context);
    if (JS_GetProperty(context, buffer_obj, name, &typed_array_val)) {
        if (typed_array_val.isNull() || typed_array_val.isUndefined()) {
            return true;
        }
        RootedObject typed_array(context, &typed_array_val.toObject());
        RootedValue array_buffer_val(context);
        if (JS_GetProperty(context, typed_array, "buffer", &array_buffer_val)) {
            RootedObject array_buffer(context, &array_buffer_val.toObject());
            if (JS_StealArrayBufferContents(context, array_buffer)) {
                return true;
            }
        }
    }
    return false;
}

static JSClass handle_class = {
    "handle_class",
    JSCLASS_HAS_PRIVATE,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL, NULL, NULL,
    NULL,
    { }
};

Value wrap_scalar(JSContext *context, const Type &t, const void *val_ptr) {
    Value result;
    if (t.is_handle()) {
        RootedObject temp(context);
        temp = JS_NewObject(context, &handle_class);
        JS_SetPrivate(temp, *(void **)val_ptr);
        result.setObject(*temp);
    } else {
        if (t.is_uint()) {
            switch (t.bits()) {
                case 1:
                case 8:
                    result.setInt32(*reinterpret_cast<const uint8_t *>(val_ptr));
                    break;
                case 16:
                    result.setInt32(*reinterpret_cast<const uint16_t *>(val_ptr));
                    break;
                case 32:
                    result.setDouble(*reinterpret_cast<const uint32_t *>(val_ptr));
                    break;
                default:
                    internal_error << "Unsupported bit size.\n";
                    result.setInt32(*reinterpret_cast<const uint8_t *>(val_ptr));
                    break;
            }
        } else if (t.is_int()) {
            switch (t.bits()) {
                case 8:
                    result.setInt32(*reinterpret_cast<const int8_t *>(val_ptr));
                    break;
                case 16:
                    result.setInt32(*reinterpret_cast<const int16_t *>(val_ptr));
                    break;
                case 32:
                    result.setInt32(*reinterpret_cast<const int32_t *>(val_ptr));
                    break;
                default:
                    internal_error << "Unsupported bit size.\n";
                    result.setInt32(*reinterpret_cast<const int8_t *>(val_ptr));
                    break;
            }
         } else if (t.is_float()) {
             switch (t.bits()) {
                 case 32:
                     result.setDouble(*reinterpret_cast<const float *>(val_ptr));
                     break;
                 case 64:
                     result.setDouble(*reinterpret_cast<const double *>(val_ptr));
                     break;
                 default:
                     internal_error << "Unsupported bit size.\n";
                     result.setDouble(*reinterpret_cast<const float *>(val_ptr));
                     break;
             }
        }
    }
    return result;
}

JITUserContext *get_user_context(HandleValue arg) {
    if (arg.isNull()) {
        return NULL;
    }
    return (JITUserContext *)JS_GetPrivate(&arg.toObject());
}

bool error_callback(JSContext *context, unsigned argc, JS::Value *vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    internal_assert(args.length() >= 2) << "Not enough arguments to error_callback in JavaScriptExecutor(SpiderMonkey).\n";

    JITUserContext *jit_user_context = get_user_context(args[0]);
    RootedString arg_str(context, JS::ToString(context, args[1]));
    char *msg = JS_EncodeStringToUTF8(context, arg_str);

    if (jit_user_context != NULL && jit_user_context->handlers.custom_error != NULL) {
        (*jit_user_context->handlers.custom_error)(jit_user_context, msg);
    } else {
        halide_runtime_error << msg;
    }

    JS_free(context, msg);

    args.rval().setInt32(0);
    return true;
}

bool print_callback(JSContext *context, unsigned argc, JS::Value *vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JITUserContext *jit_user_context = get_user_context(args[0]);
    RootedString arg_str(context, JS::ToString(context, args[1]));
    char *msg = JS_EncodeStringToUTF8(context, arg_str);

    if (jit_user_context != NULL && jit_user_context->handlers.custom_print != NULL) {
        (*jit_user_context->handlers.custom_print)(jit_user_context, msg);
    } else {
      debug(0) << msg;
    }

    JS_free(context, msg);

    args.rval().setInt32(0);
    return true;
}

std::unique_ptr<uint8_t> make_trace_value(JSContext *context, HandleValue val,
                                          int32_t type_code, int32_t bits, int32_t vector_width) {
    if (val.isUndefined() || val.isNull()) {
        return std::unique_ptr<uint8_t>();
    }
    RootedObject val_array(context, &val.toObject());
    RootedValue temp(context);
    size_t elem_size = ((bits + 7) / 8);
    size_t total_size = elem_size * vector_width;
    std::unique_ptr<uint8_t> result(new uint8_t(total_size));
    uint8_t *ptr = result.get();
    for (int32_t i = 0; i < vector_width; i++) {
        JS_GetElement(context, val_array, (uint32_t)i, &temp);
        if (type_code == 0) {
            if (bits == 8) {
                *(int8_t *)ptr = (int8_t)temp.toInt32();
            } else if (bits == 16) {
                *(int16_t *)ptr = (int16_t)temp.toInt32();
            } else if (bits == 32) {
                *(int32_t *)ptr = (int32_t)temp.toInt32();
            } else {
                *(int64_t *)ptr = (int64_t)temp.toDouble();
            }
        } else if (type_code == 1) {
            if (bits == 8) {
                *(uint8_t *)ptr = (uint8_t)temp.toDouble();
            } else if (bits == 16) {
                *(uint16_t *)ptr = (uint16_t)temp.toDouble();
            } else if (bits == 32) {
                *(uint32_t *)ptr = (uint32_t)temp.toDouble();
            } else {
                *(uint64_t *)ptr = (uint64_t)temp.toDouble();
            }
        } else if (type_code == 2) {
            internal_assert(bits >= 32) << "Tracing a bad type in JavaScript (SpiderMonkey)";
            if (bits == 32) {
                *(float *)ptr = (float)temp.toDouble();
            } else {
                *(double *)ptr = (double)temp.toNumber();
            }
        } else if (type_code == 3) {
            *(void **)ptr = (void *)temp.asRawBits();
        }
        ptr += elem_size;
    }

    return result;
}

bool trace_callback(JSContext *context, unsigned argc, JS::Value *vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    JITUserContext *jit_user_context = get_user_context(args[0]);
    RootedObject js_event(context);
    js_event = &args[1].toObject();
    halide_trace_event event;

    RootedValue temp(context);
    JS_GetProperty(context, js_event, "func", &temp);
    RootedString func_str(context, JS::ToString(context, temp));
    char *func_save = JS_EncodeStringToUTF8(context, func_str);
    event.func = func_save;
    JS_GetProperty(context, js_event, "event", &temp);
    event.event = (halide_trace_event_code_t)temp.toInt32();
    JS_GetProperty(context, js_event, "parent_id", &temp);
    event.parent_id = temp.toInt32();
    JS_GetProperty(context, js_event, "type_code", &temp);
    event.type.code = (halide_type_code_t)temp.toInt32();
    JS_GetProperty(context, js_event, "bits", &temp);
    event.type.bits = temp.toInt32();
    JS_GetProperty(context, js_event, "vector_width", &temp);
    event.type.lanes = temp.toInt32();
    JS_GetProperty(context, js_event, "value_index", &temp);
    event.value_index = temp.toInt32();
    JS_GetProperty(context, js_event, "value", &temp);
    std::unique_ptr<uint8_t> value_storage(make_trace_value(context, temp, event.type.code, event.type.bits, event.type.lanes));
    event.value = (void *)value_storage.get();
    JS_GetProperty(context, js_event, "dimensions", &temp);
    event.dimensions = (halide_trace_event_code_t)temp.toInt32();

    std::vector<int32_t> coordinates(event.dimensions);
    RootedObject js_coords(context);
    JS_GetProperty(context, js_event, "coordinates", &temp);
    js_coords = &temp.toObject();
    for (int32_t i = 0; i < event.dimensions; i++) {
        JS_GetElement(context, js_coords, i, &temp);
        coordinates[i] = temp.toInt32();
    }
    event.coordinates = &coordinates[0];

    if (jit_user_context != NULL && jit_user_context->handlers.custom_trace != NULL) {
        (*jit_user_context->handlers.custom_trace)(jit_user_context, &event);
    } else {
        // TODO: handle this case.
    }

    JS_free(context, func_save);

    int result = 0;
    args.rval().setInt32(result);
    return true;
}

bool get_array_buffer_from_typed_array_field(JSContext *context, HandleObject buffer_obj, const char *name, MutableHandleObject result) {
    RootedValue typed_array_val(context);
    if (JS_GetProperty(context, buffer_obj, name, &typed_array_val)) {
        if (!typed_array_val.isNull() && !typed_array_val.isUndefined()) {
            RootedObject typed_array(context, &typed_array_val.toObject());
            RootedValue array_buffer_val(context);
            if (JS_GetProperty(context, typed_array, "buffer", &array_buffer_val)) {
                result.set(&array_buffer_val.toObject());
                return true;
            }
        }
    }
    return false;
}

void copy_out_int32_array(JSContext *context, HandleObject buffer_obj, const char *name, int32_t *result, size_t result_length) {
    RootedValue array_val(context);
    JS_GetProperty(context, buffer_obj, name, &array_val);
    RootedObject array(context, &array_val.toObject());
    uint32_t array_length = 0;
    JS_GetArrayLength(context, array, &array_length);
    for (size_t i = 0; i < result_length; i++) {
      result[i] = 0;
    }
    result_length = std::min(result_length, (size_t)array_length);
    for (int32_t i = 0; i < (int32_t)result_length; i++) {
        RootedValue temp(context);
        RootedId index(context);
        JS_IndexToId(context, i, &index);
        JS_GetPropertyById(context, array, index, &temp);
        result[i] = temp.toInt32();
    }
}

void js_buffer_t_to_struct(JSContext *context, HandleValue val, struct buffer_t *slot) {
    RootedObject buffer_obj(context, &val.toObject());

    uint32_t length = 0;
    uint8_t *data = NULL;
    RootedObject array_buffer(context);

    if (get_array_buffer_from_typed_array_field(context, buffer_obj, "host", &array_buffer)) {
        bool is_shared_memory = false;
        GetArrayBufferLengthAndData(array_buffer, &length, &is_shared_memory, &data);
    }
    slot->host = data;

    // TODO: support GPU stuff....
    slot->dev = 0;

    copy_out_int32_array(context, buffer_obj, "min", slot->min, sizeof(slot->min) / sizeof(slot->min[0]));
    copy_out_int32_array(context, buffer_obj, "extent", slot->extent, sizeof(slot->extent) / sizeof(slot->extent[0]));
    copy_out_int32_array(context, buffer_obj, "stride", slot->stride, sizeof(slot->stride) / sizeof(slot->stride[0]));

    RootedValue temp(context);
    JS_GetProperty(context, buffer_obj, "elem_size", &temp);
    slot->elem_size = temp.toInt32();
    JS_GetProperty(context, buffer_obj, "host_dirty", &temp);
    slot->host_dirty = JS::ToBoolean(temp);
    JS_GetProperty(context, buffer_obj, "dev_dirty", &temp);
    slot->dev_dirty = JS::ToBoolean(temp);
}

template <typename T>
void val_to_slot(HandleValue val, uint64_t *slot) {
    T js_value = (T)val.toNumber();
    *(T *)slot = js_value;
}

void js_value_to_uint64_slot(const Halide::Type &type, HandleValue val, uint64_t *slot) {
    if (type.is_handle()) {
        *(void **)slot = get_user_context(val);
    } else if (type.is_float()) {
        if (type.bits() == 32) {
            val_to_slot<float>(context, val, slot);
        } else {
            internal_assert(type.bits() == 64) << "Floating-point type that isn't 32 or 64-bits wide.\n";
            val_to_slot<double>(context, val, slot);
        }
    } else if (type.is_uint()) {
        if (type.bits() == 1) {
            val_to_slot<bool>(context, val, slot);
        } else if (type.bits() == 8) {
            val_to_slot<uint8_t>(context, val, slot);
        } else if (type.bits() == 16) {
            val_to_slot<uint16_t>(context, val, slot);
        } else if (type.bits() == 32) {
            val_to_slot<uint32_t>(context, val, slot);
        } else if (type.bits() == 64) {
            user_error << "Unsigned 64-bit integer types are not supported with JavaScript.\n";
            val_to_slot<uint64_t>(context, val, slot);
        }
    } else {
        if (type.bits() == 1) {
            val_to_slot<bool>(context, val, slot);
        } else if (type.bits() == 8) {
            val_to_slot<int8_t>(context, val, slot);
        } else if (type.bits() == 16) {
            val_to_slot<int16_t>(context, val, slot);
        } else if (type.bits() == 32) {
            val_to_slot<int32_t>(context, val, slot);
        } else if (type.bits() == 64) {
            user_error << "64-bit integer types are not supported with JavaScript.\n";
            val_to_slot<int64_t>(context, val, slot);
        }
    }
    //    debug(0) << "Slot is " << *slot << "(or " << *(float *)slot << ")\n";
}

void copy_in_int32_array(JSContext *context, MutableHandleObject buffer_obj, const char *name, const int32_t *source, size_t source_length) {
    // This code is careful not to allocate a new array if an adequate one is already present as
    // the passe din object may have a typed array or be a proxy on a buffer_t.
    RootedValue array_val(context);
    JS_GetProperty(context, buffer_obj, name, &array_val);
    RootedObject array(context);
    if (array_val.isUndefined()) {
        array = JS_NewArrayObject(context, source_length);
    } else {
        array = &array_val.toObject();
    }
    uint32_t array_length = 0;
    JS_GetArrayLength(context, array, &array_length);
    if ((size_t)array_length != source_length) {
        JS_SetArrayLength(context, array, (uint32_t)source_length);
    }
    for (int32_t i = 0; i < (int32_t)source_length; i++) {
        RootedValue temp(context);
        RootedId index(context);
        JS_IndexToId(context, i, &index);
        temp.setInt32(source[i]);
        JS_SetPropertyById(context, array, index, temp);
    }
}

void buffer_t_struct_to_js(JSContext *context, const buffer_t *slot, HandleValue val) {
    RootedObject buffer_obj(context, &val.toObject());
    RootedObject array_buffer(context);

    // If there was host data, this is not a bounds query and results do not need to be copied back.
    if (get_array_buffer_from_typed_array_field(context, buffer_obj, "host", &array_buffer)) {
        return;
    }

    copy_in_int32_array(context, &buffer_obj, "min", slot->min, sizeof(slot->min) / sizeof(slot->min[0]));
    copy_in_int32_array(context, &buffer_obj, "extent", slot->extent, sizeof(slot->extent) / sizeof(slot->extent[0]));
    copy_in_int32_array(context, &buffer_obj, "stride", slot->stride, sizeof(slot->stride) / sizeof(slot->stride[0]));

    RootedValue temp(context);
    temp.setInt32(slot->elem_size);
    JS_SetProperty(context, buffer_obj, "elem_size", temp);
    temp.setBoolean(slot->host_dirty);
    JS_SetProperty(context, buffer_obj, "host_dirty", temp);
    temp.setBoolean(slot->dev_dirty);
    JS_SetProperty(context, buffer_obj, "dev_dirty", temp);
}

template <typename T, typename S>
void slot_to_return_val(const uint64_t *slot, MutableHandleValue val) {
    T slot_value = *(T *)slot;
    val.setDouble((S)slot_value);
}

void uint64_slot_to_return_value(const Halide::Type &type, const uint64_t *slot, MutableHandleValue val) {
    if (type.is_handle()) {
        internal_error << "Returning handles to JavaScript is not supported.\n";
    } else if (type.is_float()) {
        if (type.bits() == 32) {
            slot_to_return_val<float, double>(slot, val);
        } else {
            internal_assert(type.bits() == 64) << "Floating-point type that isn't 32 or 64-bits wide.\n";
            slot_to_return_val<double, double>(slot, val);
        }
    } else if (type.is_uint()) {
        if (type.bits() == 1) {
          slot_to_return_val<bool, bool>(slot, val);
        } else if (type.bits() == 8) {
          slot_to_return_val<uint8_t, uint32_t>(slot, val);
        } else if (type.bits() == 16) {
          slot_to_return_val<uint16_t, uint32_t>(slot, val);
        } else if (type.bits() == 32) {
          slot_to_return_val<uint32_t, uint32_t>(slot, val);
        } else if (type.bits() == 64) {
            user_error << "Unsigned 64-bit integer types are not supported with JavaScript.\n";
            slot_to_return_val<uint64_t, double>(slot, val);
        }
    } else {
        if (type.bits() == 1) {
          slot_to_return_val<bool, bool>(slot, val);
        } else if (type.bits() == 8) {
          slot_to_return_val<int8_t, int32_t>(slot, val);
        } else if (type.bits() == 16) {
          slot_to_return_val<int16_t, int32_t>(slot, val);
        } else if (type.bits() == 32) {
          slot_to_return_val<int32_t, int32_t>(slot, val);
        } else if (type.bits() == 64) {
            user_error << "64-bit integer types are not supported with JavaScript.\n";
            slot_to_return_val<int64_t, double>(slot, val);
        }
    }
}

struct CallbackInfo {
    ExternSignature extern_signature;
    void (*trampoline)(void **args);
};

bool extern_wrapper(JSContext *context, unsigned argc, JS::Value *vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    RootedObject callee(context, &args.callee());

    RootedValue callback_info_holder_val(context);
    JS_GetProperty(context, callee, "trampoline", &callback_info_holder_val);
    RootedObject callback_info_holder(context, &callback_info_holder_val.toObject());

    CallbackInfo *callback_info = (CallbackInfo *)JS_GetPrivate(callback_info_holder);
    ExternSignature *extern_signature = &callback_info->extern_signature;

    // Each scalar args is stored in a 64-bit slot
    size_t scalar_args_count = 0;
    // Each buffer_t gets a slot in an array of buffer_t structs
    size_t buffer_t_args_count = 0;

    std::vector<void *> trampoline_args;
    for (const Type &arg_type : extern_signature->arg_types()) {
        if (arg_type == type_of<struct buffer_t *>()) {
            buffer_t_args_count++;
        } else {
            scalar_args_count++;
        }
    }

    std::valarray<struct buffer_t> buffer_t_args(buffer_t_args_count);
    std::valarray<uint64_t> scalar_args(scalar_args_count);

    size_t args_index = 0;
    size_t buffer_t_arg_index = 0;
    size_t scalar_arg_index = 0;
    for (const Type &arg_type : extern_signature->arg_types()) {
        if (arg_type == type_of<struct buffer_t *>()) {
            js_buffer_t_to_struct(context, args[args_index++], &buffer_t_args[buffer_t_arg_index]);
            trampoline_args.push_back(&buffer_t_args[buffer_t_arg_index++]);
        } else {
            js_value_to_uint64_slot(arg_type, args[args_index++], &scalar_args[scalar_arg_index]);
            trampoline_args.push_back(&scalar_args[scalar_arg_index++]);
        }
    }

    uint64_t ret_val;
    if (!extern_signature->is_void_return()) {
        trampoline_args.push_back(&ret_val);
    }
    (*callback_info->trampoline)(&trampoline_args[0]);

    args_index = 0;
    buffer_t_arg_index = 0;
    for (const Type &arg_type : extern_signature->arg_types()) {
        if (arg_type == type_of<struct buffer_t *>()) {
            buffer_t_struct_to_js(context, &buffer_t_args[buffer_t_arg_index++], args[args_index]);
        }
        args_index++;
        // No need to retrieve scalar args as they are passed by value.
    }

    if (!extern_signature->is_void_return()) {
        uint64_slot_to_return_value(extern_signature->ret_type(), &ret_val, args.rval());
    }

    return true;
}

bool make_callbacks(JSContext *context, HandleObject global) {
    if (!JS_DefineFunction(context, global, "halide_error", &error_callback, 2, 0)) {
        return false;
    }
    if (!JS_DefineFunction(context, global, "halide_print", &print_callback, 2, 0)) {
        return false;
    }
    if (!JS_DefineFunction(context, global, "halide_trace", &trace_callback, 2, 0)) {
        return false;
    }
    return true;    // Create a template for the global object where we set the
}

bool add_extern_callbacks(JSContext *context, HandleObject global,
                          const JITExternMap &externs,
                          JITModule trampolines,
                          std::vector<CallbackInfo> &callback_storage) {
    size_t storage_index = 0;
    for (const std::pair<std::string, JITExtern> &jit_extern : externs) {
        JSFunction *f = JS_DefineFunction(context, global, jit_extern.first.c_str(), &extern_wrapper, 0, 0);
        if (f == NULL) {
            return false;
        }
        callback_storage[storage_index].extern_signature = jit_extern.second.extern_c_function().signature();
        callback_storage[storage_index].trampoline =
            (void (*)(void **))trampolines.exports().find(jit_extern.first + "_js_trampoline")->second.address;
        RootedObject temp(context);
        temp = JS_NewObject(context, &handle_class);
        JS_SetPrivate(temp, &callback_storage[storage_index]);
        RootedObject holder(context, JS_GetFunctionObject(f));
        JS_DefineProperty(context, holder, "trampoline", temp, JSPROP_READONLY | JSPROP_ENUMERATE);
        storage_index++;
    }
    return true;
}

// SpiderMonkey basically insists on a one to one mapping between its JSRuntime
// data structure and a thread. Though aruntime can be destroyed and then a
// new one made on the same thread. The issue is if two Funcs are compiled
// on a thread and their lifetimes overlap, they need to use the same JSRuntime.
// However we do want to free the resources used by the runtime if it is not in use.
// The design here is to assume single threading, which is true for JS tests, and
// to reference count the runtime so it can be freed if there are no uses.

} // namespace JS_SpiderMonkey

namespace {
JSRuntime *current_spider_monkey_runtime = nullptr;
uint32_t current_spider_monkey_runtime_refs = 0;
JSRuntime *spider_monkey_get_runtime() {
    if (current_spider_monkey_runtime == nullptr) {
        internal_assert(current_spider_monkey_runtime_refs == 0) << "Current Spider Moneky runtime is nullptr with refcount non-zero.\n";
        current_spider_monkey_runtime = JS_NewRuntime(128L * 1024L * 1024L);
    }
    if (current_spider_monkey_runtime != nullptr) {
        current_spider_monkey_runtime_refs++;
    }
    return current_spider_monkey_runtime;
}
}

void spider_monkey_release_runtime() {
    internal_assert(current_spider_monkey_runtime_refs > 0) << "Releasing Spider Moneky runtime with refcount at zero.\n";
    if (--current_spider_monkey_runtime_refs == 0) {
        JS_DestroyRuntime(current_spider_monkey_runtime);
        current_spider_monkey_runtime = nullptr;
    }
}

int compile_javascript_spider_monkey(const std::string &source, const std::string &fn_name,
                                     const JITExternMap &externs,
                                     JITModule trampolines,
                                     JSRuntime *&runtime, JSContext *&context,
                                     JS::PersistentRootedObject &global_holder, std::string &function_name,
                                     std::vector<JS_SpiderMonkey::CallbackInfo> &callback_info_storage) {
    using namespace JS_SpiderMonkey;

    debug(0) << "Calling JavaScript function " << fn_name << "\n";
    // TODO: thread safety.
    static bool inited = false;
    if (!inited) {
        if (!JS_Init()) {
            return -1;
        }
        inited = true;
    }

    // Create a JS runtime.
    runtime = spider_monkey_get_runtime();
    if (!runtime) {
        return -1;
    }

    // Create a context.
    context = JS_NewContext(runtime, 8192);
    if (!context) {
        spider_monkey_release_runtime();
        return -1;
    }
    JS_SetErrorReporter(runtime, reportError);

    JSAutoRequest request(context);

    {
        // Create the global object and a new compartment.
        RootedObject global(context);
        JS::CompartmentOptions compartment_options;
        global = JS_NewGlobalObject(context, &global_class, NULL,
                                    JS::DontFireOnNewGlobalHook, compartment_options);
        if (!global) {
            JS_DestroyContext(context);
            spider_monkey_release_runtime();
            return -1;
        }
        global_holder.init(context, global);

        JSAutoCompartment compartment(context, global);

        // Populate the global object with the standard globals, like Object and
        // Array.
        if (!JS_InitStandardClasses(context, global)) {
            JS_DestroyContext(context);
            spider_monkey_release_runtime();
            return -1;
        }

        make_callbacks(context, global);

        callback_info_storage.resize(externs.size());
        if (!add_extern_callbacks(context, global, externs, trampolines, callback_info_storage)) {
            debug(0) << "Failure adding extern callbacks to SpiderMonkey globals.\n";
            JS_DestroyContext(context);
            spider_monkey_release_runtime();
            return -1;
        }

        JS_FireOnNewGlobalObject(context, global);

        RootedValue script_result(context);
        CompileOptions options(context);
        bool succeeded = JS::Evaluate(context, options, source.data(), source.size(), &script_result);
        if (!succeeded) {
            debug(0) << "JavaScript script evaulation failed(SpiderMonkey).\n";
            JS_DestroyContext(context);
            spider_monkey_release_runtime();
            return -1;
        }

        debug(0) << "Script compiled(SpiderMonkey).\n";

        function_name = fn_name;
    }

    return 0;
}

int run_javascript_spider_monkey(std::vector<std::pair<Argument, const void *>> args,
                                 JSContext *context, JS::PersistentRootedObject &global_holder,
                                 const std::string &fn_name) {
    int32_t result = 0;

    using namespace JS_SpiderMonkey;

    JSAutoRequest request(context);
    JSAutoCompartment compartment(context, global_holder);

    debug(0) << "Calling JavaScript function " << fn_name << " with " << args.size() << " args.\n";

    AutoValueVector js_args(context);
    for (size_t i = 0; i < args.size(); i++) {
        Argument &arg(args[i].first);
        if (arg.is_buffer()) {
            js_args.append(make_buffer_t(context,
                                         (struct buffer_t *)args[i].second,
                                         halide_type_to_external_array_type(arg.type)));
        } else {
            js_args.append(wrap_scalar(context, arg.type, args[i].second));
        }
    }

    RootedValue js_result(context);
    int succeeded = JS::Call(context, global_holder, fn_name.c_str(), js_args, &js_result);

    debug(0) << "Returned from call with return val " << succeeded << ".\n";

    // The underlying memory for the array buffers must be stolen back
    // so GC doesn't try to free the pointers.
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].first.is_buffer()) {
            disconnect_array_buffer(context, js_args[i], "host");
            disconnect_array_buffer(context, js_args[i], "min");
            disconnect_array_buffer(context, js_args[i], "stride");
            disconnect_array_buffer(context, js_args[i], "extent");
        }
    }

    if (succeeded) {
        JS::ToInt32(context, js_result, &result);
    } else {
      result = -1;
    }

    return result;
}

}} // close Halide::Internal namespace

#endif

namespace Halide { namespace Internal {

struct JavaScriptModuleContents {
    mutable RefCount ref_count;

    JITExternMap externs;
    std::vector<JITModule> extern_deps;
    JITModule trampolines;

#ifdef WITH_JAVASCRIPT_V8
    v8::Isolate *v8_isolate;
    v8::Persistent<v8::Context> v8_context;
    v8::Persistent<v8::Function> v8_function;
#endif

#ifdef WITH_JAVASCRIPT_SPIDERMONKEY
    JSRuntime *spider_monkey_runtime;
    JSContext *spider_monkey_context;
    JS::PersistentRootedObject spider_monkey_globals;
    std::string spider_monkey_function_name;
    std::vector<JS_SpiderMonkey::CallbackInfo> spider_monkey_callback_info_storage;
#endif

JavaScriptModuleContents() {
#ifdef WITH_JAVASCRIPT_V8
    v8_isolate = nullptr;
#endif
#ifdef WITH_JAVASCRIPT_SPIDERMONKEY
    spider_monkey_runtime = nullptr;
    spider_monkey_context = nullptr;
#endif
    }

    ~JavaScriptModuleContents() {
#ifdef WITH_JAVASCRIPT_V8
        if (v8_isolate != nullptr) {
            // Not sure if this is required...
            {
                v8::Isolate::Scope isolate_scope(v8_isolate);

                v8_function.Reset();
                v8_context.Reset();
            }

            v8_isolate->Dispose();
        }
#endif

#ifdef WITH_JAVASCRIPT_SPIDERMONKEY
        spider_monkey_globals.reset();
        if (spider_monkey_context != nullptr) {
            JS_DestroyContext(spider_monkey_context);
        }
        if (spider_monkey_runtime != nullptr) {
            internal_assert(spider_monkey_runtime == current_spider_monkey_runtime) << "Releasing JSRuntime that is not the current one.\n";
            spider_monkey_release_runtime();
        }
#endif
    }
};

template<>
RefCount &ref_count<JavaScriptModuleContents>(const JavaScriptModuleContents *p) {
    return p->ref_count;
}

template<>
void destroy<JavaScriptModuleContents>(const JavaScriptModuleContents *p) {
    delete p;
}

JavaScriptModule compile_javascript(
  const Target &target,
  const std::string &source,
  const std::string &fn_name,
  const JITExternMap &externs,
  const std::vector<JITModule> &extern_deps
) {
#if !defined(WITH_JAVASCRIPT_V8) && !defined(WITH_JAVASCRIPT_SPIDERMONKEY)
    user_error << "Cannot run JITted JavaScript without configuring a JavaScript engine.";
    return JavaScriptModule();
#endif

    JavaScriptModule module;
    module.contents = new JavaScriptModuleContents();

    module.contents->externs = externs;
    module.contents->extern_deps = extern_deps;
    // This call explicitly does not use "_argv" as the suffix because
    // that name may already exist and if so, will return an int
    // instead of taking a pointer at the end of the args list to
    // receive the result value.
    module.contents->trampolines = JITModule::make_trampolines_module(target, module.contents->externs,
                                                                      "_js_trampoline", module.contents->extern_deps);

#ifdef WITH_JAVASCRIPT_V8
    if (!target.has_feature(Target::JavaScript_SpiderMonkey)) {
        if (compile_javascript_v8(source, fn_name, module.contents->externs, module.contents->trampolines,
                                  module.contents->v8_isolate, module.contents->v8_context,
                                  module.contents->v8_function) == 0) {
            return module;
        }
    }
#else
    if (target.has_feature(Target::JavaScript_V8)) {
        user_error << "V8 JavaScript requested without configuring V8 JavaScript engine.";
    }
#endif

#ifdef WITH_JAVASCRIPT_SPIDERMONKEY
    debug(0) << "Compiling with SpiderMonkey\n";
    if (compile_javascript_spider_monkey(source, fn_name, module.contents->externs,
                                         module.contents->trampolines, module.contents->spider_monkey_runtime,
                                         module.contents->spider_monkey_context, module.contents->spider_monkey_globals,
                                         module.contents->spider_monkey_function_name,
                                         module.contents->spider_monkey_callback_info_storage) == 0) {
        debug(0) << "Compiling with SpiderMonkey suceeded\n";
        return module;
    }
#else
    if (target.has_feature(Target::JavaScript_SpiderMonkey)) {
        user_error << "SpiderMonkey JavaScript requested without configuring SpiderMonkey JavaScript engine.";
    }
#endif

    module.contents = nullptr;
    return module;
}

/** Run generated previously compiled JavaScript code with a set of arguments. */
int run_javascript(JavaScriptModule module, const std::vector<std::pair<Argument, const void *>> &args) {
#if !defined(WITH_JAVASCRIPT_V8) && !defined(WITH_JAVASCRIPT_SPIDERMONKEY)
    user_error << "Cannot run JITted JavaScript without configuring a JavaScript engine.";
    return -1;
#endif

#ifdef WITH_JAVASCRIPT_V8
    if (module.contents->v8_isolate) {
        return run_javascript_v8(args, module.contents->v8_isolate,
                                 module.contents->v8_context, module.contents->v8_function);
    }
#endif

#ifdef WITH_JAVASCRIPT_SPIDERMONKEY
    debug(0) << "Running with SpiderMonkey\n";
    if (module.contents->spider_monkey_runtime) {
        return run_javascript_spider_monkey(args, module.contents->spider_monkey_context,
                                            module.contents->spider_monkey_globals,
                                            module.contents->spider_monkey_function_name);
    }
#endif

    return -1;
}

}} // close Halide::Internal namespace
