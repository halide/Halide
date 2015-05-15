#include "Error.h"
#include "JavaScriptExecutor.h"
#include "JITModule.h"
#include "Func.h"
#include "Target.h"

#include "runtime/HalideRuntime.h"
#include <valarray>
#include <vector>

// Avoid unused function warning if not configured with JavaScript.
// TODO: Move routine to Utils.h
#if WITH_JAVASCRIPT_V8 || WITH_JAVASCRIPT_SPIDERMONKEY

namespace {

int32_t buffer_total_size(const buffer_t *buf) {
    int32_t total_size = 1;
    for (int i = 0; i < 4; i++) {
        int32_t stride = buf->stride[i];
        if (stride < 0) stride = -stride;
        if ((buf->extent[i] * stride) > total_size) {
            total_size = buf->extent[i] * stride;
        }
    }
    return total_size;
}

}

namespace Halide { namespace Internal {

// TODO: Filter math routines, runtime routines, etc.
std::map<std::string, Halide::JITExtern> filter_externs(const std::map<std::string, Halide::JITExtern> &externs) {
    std::map<std::string, Halide::JITExtern> result = externs;
    result.erase("halide_print");
    return result;
}

}} // close Halide::Internal namespace

#endif

#if WITH_JAVASCRIPT_V8

#include "include/v8.h"
#include "include/libplatform/libplatform.h"

namespace Halide { namespace Internal {

namespace JS_V8 {

using namespace v8;

template <typename T>
void get_host_array(Local<String> property,
                    const PropertyCallbackInfo<Value> &info) {
    // TODO: It is likely profitable to cache the array objects here (e.g. in internal fields).
    Local<Object> obj = info.Holder();
    Local<External> buf_wrapper = Local<External>::Cast(obj->GetInternalField(0));
    const buffer_t *buf = (const buffer_t *)buf_wrapper->Value();
    String::Utf8Value str_val(property);
    //    debug(0) << "Getter from " << buf << " for " << *str_val << " host is " << (void *)buf->host << "\n";
    if (buf->host == NULL) {
        info.GetReturnValue().SetNull();  
    } else {
        int32_t total_size = buffer_total_size(buf);
        Local<ArrayBuffer> array_buf = ArrayBuffer::New(info.GetIsolate(), buf->host, total_size * buf->elem_size);
        Local<T> value = T::New(array_buf, 0, total_size);
        info.GetReturnValue().Set(value);  
    }
}

template <typename T>
void get_struct_field(Local<String> property,
                      const PropertyCallbackInfo<Value> &info) {
    Local<Object> obj = info.Holder();
    Local<External> buf_wrapper = Local<External>::Cast(obj->GetInternalField(0));
    const void *buf = buf_wrapper->Value();
    int32_t offset = info.Data()->Uint32Value();
    T value = *(const T *)((const char *)buf + offset);
    String::Utf8Value str_val(property);
    //    debug(0) << "Getter from " << buf << " for " << *str_val << " (offset " << offset << ") returning " << value << "\n";
    info.GetReturnValue().Set(value);  
}

// TODO: Figure out how to wrap 64-bit field. Probably as an Object...
void get_dev_field(Local<String> property,
                   const PropertyCallbackInfo<Value> &info) {
    info.GetReturnValue().Set(0);  
}

template <typename T>
void set_struct_field(Local<String> /* property */,
                      Local<Value> value,
                      const PropertyCallbackInfo<void> &info) {
  T coerced_value = 0;
  if (value->IsBoolean()) {
      coerced_value = (T)value->BooleanValue();
  } else if (value->IsInt32()) {
      coerced_value = (T)value->Int32Value();
  } else if (value->IsUint32()) {
      coerced_value = (T)value->Uint32Value();
  } else if (value->IsNumber()) {
      coerced_value = (T)value->NumberValue();
  } else {
      internal_error << "Unknown V8 JS type in set_struct_field\n.";
  }

  Local<Object> obj = info.Holder();
  Local<External> buf_wrapper = Local<External>::Cast(obj->GetInternalField(0));
  const void *buf = buf_wrapper->Value();
  int32_t offset = info.Data()->Int32Value();
  *(T *)((const char *)buf + offset) = coerced_value;
}

void get_buffer_t_array_field(Local<String> property,
                              const PropertyCallbackInfo<Value> &info) {
    // TODO: It is likely profitable to cache the array objects here (e.g. in internal fields).
    Local<Object> obj = info.Holder();
    Local<External> buf_wrapper = Local<External>::Cast(obj->GetInternalField(0));
    const void *buf = buf_wrapper->Value();
    int32_t offset = info.Data()->Uint32Value();
    //    const int32_t *vals = (const int32_t *)((char *)buf + offset);
    String::Utf8Value str_val(property);
    //    debug(0) << "Array getter from " << buf << " for " << *str_val << " (offset " << offset << ") returning [" << vals[0] << ", " << vals[1] << ", " << vals[2] << ", " << vals[3] << "]\n";
    Local<ArrayBuffer> array_buf = ArrayBuffer::New(info.GetIsolate(), (char *)buf + offset, 4 * sizeof(int32_t));
    Local<Int32Array> result = Int32Array::New(array_buf, 0, 4);
    info.GetReturnValue().Set(result);  
    
}

Local<ObjectTemplate> make_buffer_t_template(Isolate* isolate, ExternalArrayType element_type) {
    Local<ObjectTemplate> object_template = ObjectTemplate::New(isolate);

    AccessorGetterCallback host_getter = NULL;
    switch (element_type) {
    case kExternalInt8Array:
      debug(0) << "Making Int8Array.\n";
        host_getter = get_host_array<Int8Array>;
        break;
    case kExternalUint8Array:
      debug(0) << "Making Uint8Array.\n";
        host_getter = get_host_array<Uint8Array>;
        break;
    case kExternalInt16Array:
      debug(0) << "Making Int16Array.\n";
        host_getter = get_host_array<Int16Array>;
        break;
    case kExternalUint16Array:
      debug(0) << "Making Uint16Array.\n";
        host_getter = get_host_array<Uint16Array>;
        break;
    case kExternalInt32Array:
      debug(0) << "Making Int32Array.\n";
        host_getter = get_host_array<Int32Array>;
        break;
    case kExternalUint32Array:
      debug(0) << "Making Uint32Array.\n";
        host_getter = get_host_array<Uint32Array>;
        break;
    case kExternalFloat32Array:
      debug(0) << "Making Float32Array.\n";
        host_getter = get_host_array<Float32Array>;
        break;
    case kExternalFloat64Array:
      debug(0) << "Making Float64Array.\n";
        host_getter = get_host_array<Float64Array>;
        break;
    default:
        internal_error << "Unknown array type.\n";
        break;
    }

    object_template->SetAccessor(String::NewFromUtf8(isolate, "host"),
                                 host_getter, NULL,
                                 Integer::New(isolate, offsetof(buffer_t, host)));
    object_template->SetAccessor(String::NewFromUtf8(isolate, "dev"),
                                 get_dev_field, NULL,
                                 Integer::New(isolate, offsetof(buffer_t, dev)));
    object_template->SetAccessor(String::NewFromUtf8(isolate, "elem_size"),
                                 get_struct_field<int32_t>, set_struct_field<int32_t>,
                                 Integer::New(isolate, offsetof(buffer_t, elem_size)));
    object_template->SetAccessor(String::NewFromUtf8(isolate, "host_dirty"),
                                 get_struct_field<bool>, set_struct_field<bool>,
                                 Integer::New(isolate, offsetof(buffer_t, host_dirty)));
    object_template->SetAccessor(String::NewFromUtf8(isolate, "dev_dirty"),
                                 get_struct_field<bool>, set_struct_field<bool>,
                                 Integer::New(isolate, offsetof(buffer_t, dev_dirty)));
    object_template->SetAccessor(String::NewFromUtf8(isolate, "extent"),
                                 get_buffer_t_array_field, NULL,
                                 Integer::New(isolate, offsetof(buffer_t, extent)));
    object_template->SetAccessor(String::NewFromUtf8(isolate, "stride"),
                                 get_buffer_t_array_field, NULL,
                                 Integer::New(isolate, offsetof(buffer_t, stride)));
    object_template->SetAccessor(String::NewFromUtf8(isolate, "min"),
                                 get_buffer_t_array_field, NULL,
                                 Integer::New(isolate, offsetof(buffer_t, min)));

    object_template->SetInternalFieldCount(1);
    
    return object_template;
}

Local<Object> make_buffer_t(Isolate *isolate, struct buffer_t *buf, ExternalArrayType element_type) {
    debug(0) << "Making buffer_t on " << buf << " which has elem_size " << buf->elem_size << "\n";
    Local<ObjectTemplate> object_template = make_buffer_t_template(isolate, element_type);
    Local<Object> wrapper = object_template->NewInstance();
    Local<External> buf_wrap(External::New(isolate, buf));
    wrapper->SetInternalField(0, buf_wrap);
    return wrapper;
}

ExternalArrayType halide_type_to_external_array_type(const Type &t) {
  if (t.is_uint()) {
      switch (t.bits) {
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
       switch (t.bits) {
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
       switch (t.bits) {
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

Local<Value> wrap_scalar(Isolate *isolate, const Type &t, const void *val_ptr) {
    if (t.is_handle()) {
        Local<ObjectTemplate> object_template = ObjectTemplate::New(isolate);
        object_template->SetInternalFieldCount(1);
        Local<Object> wrapper = object_template->NewInstance();
        Local<External> handle_wrap(External::New(isolate, *(void **)val_ptr));
        wrapper->SetInternalField(0, handle_wrap);
        return wrapper;
    } else {
        double val = 0;
        if (t.is_uint()) {
            switch (t.bits) {
                case 1:
                case 8:
                    val = *reinterpret_cast<const uint8_t *>(val_ptr);
                    break;
                case 16:
                    val = *reinterpret_cast<const uint16_t *>(val_ptr);
                    break;
                case 32:
                    val = *reinterpret_cast<const uint32_t *>(val_ptr);
                    break;
                default:
                    internal_error << "Unsupported bit size.\n";
                    val = *reinterpret_cast<const uint8_t *>(val_ptr);
                    break;
            }
        } else if (t.is_int()) {
            switch (t.bits) {
                case 8:
                    val = *reinterpret_cast<const int8_t *>(val_ptr);
                    break;
                case 16:
                    val = *reinterpret_cast<const int16_t *>(val_ptr);
                    break;
                case 32:
                    val = *reinterpret_cast<const int32_t *>(val_ptr);
                    break;
                default:
                    internal_error << "Unsupported bit size.\n";
                    val = *reinterpret_cast<const int8_t *>(val_ptr);
                    break;
            }
         } else if (t.is_float()) {
             switch (t.bits) {
                 case 32:
                     val = *reinterpret_cast<const float *>(val_ptr);
                     break;
                 case 64:
                     val = *reinterpret_cast<const double *>(val_ptr);
                     break;
                 default:
                     internal_error << "Unsupported bit size.\n";
                     val = *reinterpret_cast<const float *>(val_ptr);
                     break;
             }
         }

        return Number::New(isolate, val);
   }
}

void dump_object(Local<Object> &obj) {
    Local<Array> names = obj->GetPropertyNames();

    //    debug(0) << "Dumping object names for obj with " << names->Length() << " names.\n";
    for (uint32_t i = 0; i < names->Length(); i++) {
        Local<Value> name = names->Get(i);
        internal_assert(name->IsString()) << "Name is not a string.\n";
        String::Utf8Value printable(name);
        //      debug(0) << "object has key: " << *printable << "\n";
    }
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

void print_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
  //  debug(0) << "print callback called with " << args.Length() << " args\n";
    internal_assert(args.Length() >= 2) << "Not enough arguments to print_callback in JavaScriptExecutor.\n";
    HandleScope scope(args.GetIsolate());
    Local<Object> user_context = args[0]->ToObject();
    Local<External> handle_wrapper = Local<External>::Cast(user_context->GetInternalField(0));
    JITUserContext *jit_user_context = (JITUserContext *)handle_wrapper->Value();
    Local<Value> arg = args[1];
    String::Utf8Value value(arg);

    if (jit_user_context->handlers.custom_print != NULL) {
        (*jit_user_context->handlers.custom_print)(jit_user_context, *value);
    } else {
        // TODO: Figure out a better way to send output...
        debug(0) << *value;
    }
}

void error_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
  //  debug(0) << "error callback called with " << args.Length() << " args\n";
    internal_assert(args.Length() >= 2) << "Not enough arguments to error_callback in JavaScriptExecutor(V8).\n";
    HandleScope scope(args.GetIsolate());
    Local<Object> user_context = args[0]->ToObject();
    Local<External> handle_wrapper = Local<External>::Cast(user_context->GetInternalField(0));
    JITUserContext *jit_user_context = (JITUserContext *)handle_wrapper->Value();
    Local<Value> arg = args[1];
    String::Utf8Value value(arg);

    if (jit_user_context->handlers.custom_error != NULL) {
        (*jit_user_context->handlers.custom_error)(jit_user_context, *value);
    } else {
        halide_runtime_error << *value;
    }
}

std::unique_ptr<uint8_t> make_trace_value(Local<Object> val_array, int32_t type_code, int32_t bits, int32_t vector_width) {
    if (val_array->IsUndefined() || val_array->IsNull()) {
        return std::unique_ptr<uint8_t>();
    }
    size_t elem_size = ((bits + 7) / 8);
    size_t total_size = elem_size * vector_width;
    std::unique_ptr<uint8_t> result(new uint8_t(total_size));
    uint8_t *ptr = result.get();
    for (int32_t i = 0; i < vector_width; i++) {
        if (type_code == 0) {
            if (bits == 8) {
                *(int8_t *)ptr = (int8_t)val_array->Get(i)->Int32Value();
            } else if (bits == 16) {
                *(int16_t *)ptr = (int16_t)val_array->Get(i)->Int32Value();
            } else if (bits == 32) {
                *(int32_t *)ptr = (int32_t)val_array->Get(i)->Int32Value();
            } else {
                *(int64_t *)ptr = (int64_t)val_array->Get(i)->IntegerValue();
            }
        } else if (type_code == 1) {
            if (bits == 8) {
                *(uint8_t *)ptr = (uint8_t)val_array->Get(i)->Uint32Value();
            } else if (bits == 16) {
                *(uint16_t *)ptr = (uint16_t)val_array->Get(i)->Uint32Value();
            } else if (bits == 32) {
                *(uint32_t *)ptr = (uint32_t)val_array->Get(i)->Uint32Value();
            } else {
                *(uint64_t *)ptr = (uint64_t)val_array->Get(i)->IntegerValue();
            }
        } else if (type_code == 2) {
            internal_assert(bits >= 32) << "Tracing a bad type in JavaScript";
            if (bits == 32) {
                *(float *)ptr = (float)val_array->Get(i)->NumberValue();
            } else {
                *(double *)ptr = (double)val_array->Get(i)->NumberValue();
            }
        } else if (type_code == 3) {
            *(void **)ptr = Local<External>::Cast(val_array->Get(i))->Value();
        }
        ptr += elem_size;
    }

    return result;
}

void trace_callback(const v8::FunctionCallbackInfo<v8::Value>& args) {
  //  debug(0) << "trace callback called with " << args.Length() << " args\n";
    internal_assert(args.Length() >= 2) << "Not enough arguments to trace_callback in JavaScriptExecutor.\n";
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Object> user_context = args[0]->ToObject();
    Local<External> handle_wrapper = Local<External>::Cast(user_context->GetInternalField(0));
    JITUserContext *jit_user_context = (JITUserContext *)handle_wrapper->Value();
    Local<Object> js_event = args[1]->ToObject();

    halide_trace_event event;

    Local<Value> func_name_obj = js_event->Get(String::NewFromUtf8(isolate, "func"));
    String::Utf8Value func_name(func_name_obj);
    event.func = *func_name;
    event.event = (halide_trace_event_code)js_event->Get(String::NewFromUtf8(isolate, "event"))->Int32Value();
    event.parent_id = js_event->Get(String::NewFromUtf8(isolate, "parent_id"))->Int32Value();
    event.type_code = js_event->Get(String::NewFromUtf8(isolate, "type_code"))->Int32Value();
    event.bits = js_event->Get(String::NewFromUtf8(isolate, "bits"))->Int32Value();
    event.vector_width = js_event->Get(String::NewFromUtf8(isolate, "vector_width"))->Int32Value();
    event.value_index = js_event->Get(String::NewFromUtf8(isolate, "value_index"))->Int32Value();
    std::unique_ptr<uint8_t> value_storage(make_trace_value(js_event->Get(String::NewFromUtf8(isolate, "value"))->ToObject(), event.type_code, event.bits, event.vector_width));
    event.value = (void *)value_storage.get();
    event.dimensions = js_event->Get(String::NewFromUtf8(isolate, "dimensions"))->Int32Value();
    
    std::vector<int32_t> coordinates(event.dimensions);
    Local<Object> js_coords = js_event->Get(String::NewFromUtf8(isolate, "coordinates"))->ToObject();
    for (int32_t i = 0; i < event.dimensions; i++) {
        coordinates[i] = js_coords->Get(i)->Int32Value();
    }
    event.coordinates = &coordinates[0];

    if (jit_user_context->handlers.custom_trace != NULL) {
        (*jit_user_context->handlers.custom_trace)(jit_user_context, &event);
    } else {
        // TODO: handle this case.
    }
}

void js_buffer_t_to_struct(const Local<Value> &val, struct buffer_t *slot) {
#if 0
    Local<Object> buf = val->ToObject();

    Local<TypedArray> host_array = Cast<TypedArray>(buf->Get(String::NewFromUtf8(isolate, "host")));
    Local<ArrayBuffer> array_buffer = host_array->Buffer();
#endif
}

template <typename T>
void val_to_slot(const Local<Value> &val, uint64_t *slot) {
    T js_value = (T)val->NumberValue();
    *(T *)slot = js_value;
}

void js_value_to_uint64_slot(const Halide::Type &type, const Local<Value> &val, uint64_t *slot) {
  String::Utf8Value printable(val);
  //  debug(0) << "Argument is " << *printable << "\n";
    if (type.is_handle()) {
    } else if (type.is_float()) {
        if (type.bits == 32) {
            val_to_slot<float>(val, slot);
        } else {
            internal_assert(type.bits == 64) << "Floating-point type that isn't 32 or 64-bits wide.\n";
            val_to_slot<double>(val, slot);
        }
    } else if (type.is_uint()) {
        if (type.bits == 1) {
            val_to_slot<bool>(val, slot);
        } else if (type.bits == 8) {
            val_to_slot<uint8_t>(val, slot);
        } else if (type.bits == 16) {
            val_to_slot<uint16_t>(val, slot);
        } else if (type.bits == 32) {
            val_to_slot<uint32_t>(val, slot);
        } else if (type.bits == 64) {
            user_error << "Unsigned 64-bit integer types are not supported with JavaScript.\n";
            val_to_slot<uint64_t>(val, slot);
        }
    } else {
        if (type.bits == 1) {
            val_to_slot<bool>(val, slot);
        } else if (type.bits == 8) {
            val_to_slot<int8_t>(val, slot);
        } else if (type.bits == 16) {
            val_to_slot<int16_t>(val, slot);
        } else if (type.bits == 32) {
            val_to_slot<int32_t>(val, slot);
        } else if (type.bits == 64) {
            user_error << "64-bit integer types are not supported with JavaScript.\n";
            val_to_slot<int64_t>(val, slot);
        }
    }
    //    debug(0) << "Slot is " << *slot << "(or " << *(float *)slot << ")\n";
}

void buffer_t_struct_to_js(const buffer_t *slot, Local<Value> val) {
#if 0
    Local<Object> buf = val->ToObject();
#endif
}

template <typename T, typename S>
void slot_to_return_val(const uint64_t *slot, ReturnValue<Value> val) {
    T slot_value = *(T *)slot;
    val.Set((S)slot_value);
}

void uint64_slot_to_return_value(const Halide::Type &type, const uint64_t *slot, ReturnValue<Value> val) {
    if (type.is_handle()) {
    } else if (type.is_float()) {
        if (type.bits == 32) {
            slot_to_return_val<float, double>(slot, val);
        } else {
            internal_assert(type.bits == 64) << "Floating-point type that isn't 32 or 64-bits wide.\n";
            slot_to_return_val<double, double>(slot, val);
        }
    } else if (type.is_uint()) {
        if (type.bits == 1) {
          slot_to_return_val<bool, bool>(slot, val);
        } else if (type.bits == 8) {
          slot_to_return_val<uint8_t, uint32_t>(slot, val);
        } else if (type.bits == 16) {
          slot_to_return_val<uint16_t, uint32_t>(slot, val);
        } else if (type.bits == 32) {
          slot_to_return_val<uint32_t, uint32_t>(slot, val);
        } else if (type.bits == 64) {
            user_error << "Unsigned 64-bit integer types are not supported with JavaScript.\n";
            slot_to_return_val<uint64_t, double>(slot, val);
        }
    } else {
        if (type.bits == 1) {
          slot_to_return_val<bool, bool>(slot, val);
        } else if (type.bits == 8) {
          slot_to_return_val<int8_t, int32_t>(slot, val);
        } else if (type.bits == 16) {
          slot_to_return_val<int16_t, int32_t>(slot, val);
        } else if (type.bits == 32) {
          slot_to_return_val<int32_t, int32_t>(slot, val);
        } else if (type.bits == 64) {
            user_error << "64-bit integer types are not supported with JavaScript.\n";
            slot_to_return_val<int64_t, double>(slot, val);
        }
    }
}

void v8_extern_wrapper(const v8::FunctionCallbackInfo<v8::Value>& args) {
    Isolate *isolate = args.GetIsolate();
    HandleScope scope(isolate);
    Local<Object> wrapper_data = args.Data()->ToObject();
    Local<String> extern_name = Local<String>::Cast(wrapper_data->GetInternalField(0));
    Local<External> jit_extern_wrap = Local<External>::Cast(wrapper_data->GetInternalField(1));
    const std::map<std::string, JITExtern> *jit_externs = (const std::map<std::string, JITExtern> *)jit_extern_wrap->Value();
    Local<External> trampoline_wrap = Local<External>::Cast(wrapper_data->GetInternalField(2));
    void (*trampoline)(void **) = (void (*)(void **))trampoline_wrap->Value();

    String::Utf8Value str_val(extern_name);
    auto iter = jit_externs->find(*str_val);
    internal_assert(iter != jit_externs->end()) << "jit_extern " << *str_val << " not found in map.";

    // Each scalar args is stored in a 64-bit slot
    size_t scalar_args_count = 0;
    // Each buffer_t gets a slot in an array of buffer_t structs
    size_t buffer_t_args_count = 0;

    std::vector<void *> trampoline_args;
    internal_assert(iter->second.func == NULL);
    for (const ScalarOrBufferT &scalar_or_buffer_t : iter->second.arg_types) {
        if (scalar_or_buffer_t.is_buffer) {
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
    for (const ScalarOrBufferT &scalar_or_buffer_t : iter->second.arg_types) {
        if (scalar_or_buffer_t.is_buffer) {
            js_buffer_t_to_struct(args[args_index++], &buffer_t_args[buffer_t_arg_index]);
            trampoline_args.push_back(&buffer_t_args[buffer_t_arg_index++]);
        } else {
            js_value_to_uint64_slot(scalar_or_buffer_t.scalar_type, args[args_index++], &scalar_args[scalar_arg_index]);
            trampoline_args.push_back(&scalar_args[scalar_arg_index++]);
        }
    }

    uint64_t ret_val;
    if (!iter->second.is_void_return) {
        trampoline_args.push_back(&ret_val);
    }
    (*trampoline)(&trampoline_args[0]);

    args_index = 0;
    buffer_t_arg_index = 0;
    for (const ScalarOrBufferT &scalar_or_buffer_t : iter->second.arg_types) {
        if (scalar_or_buffer_t.is_buffer) {
            buffer_t_struct_to_js(&buffer_t_args[buffer_t_arg_index], args[args_index++]);
        }
        // No need to retrieve scalar args as they are passed by value.
    }
    
    if (!iter->second.is_void_return) {
        uint64_slot_to_return_value(iter->second.ret_type, &ret_val, args.GetReturnValue());
    }
}

Local<ObjectTemplate> make_global_template(Isolate *isolate) {
    // Create a template for the global object where we set the
    // built-in global functions.
    Local<ObjectTemplate> global = ObjectTemplate::New(isolate);
    global->Set(String::NewFromUtf8(isolate, "halide_error"),
                FunctionTemplate::New(isolate, error_callback));
    global->Set(String::NewFromUtf8(isolate, "halide_print"),
                FunctionTemplate::New(isolate, print_callback));
    global->Set(String::NewFromUtf8(isolate, "halide_trace"),
                FunctionTemplate::New(isolate, trace_callback));
    return global;
}

void add_extern_callbacks(Isolate *isolate, Local<Context> &context,
                          const std::map<std::string, JITExtern> &externs,
                          JITModule trampolines) {
    for (const std::pair<std::string, JITExtern> &jit_extern : externs) {
        Local<ObjectTemplate> extern_callback_template = ObjectTemplate::New(isolate);
        extern_callback_template->SetInternalFieldCount(3);

        debug(0) << "Making callback for " << jit_extern.first << " c_function is " << jit_extern.second.c_function << "\n";
        Local<Object> wrapper_data = extern_callback_template->NewInstance();
        Local<External> jit_externs_wrap(External::New(isolate, (void *)&externs));
        Local<External> trampoline_wrap(External::New(isolate,
            const_cast<void *>(trampolines.exports().find(jit_extern.first + "_js_trampoline")->second.address)));
        wrapper_data->SetInternalField(0, String::NewFromUtf8(isolate, jit_extern.first.c_str()));
        wrapper_data->SetInternalField(1, jit_externs_wrap);
        wrapper_data->SetInternalField(2, trampoline_wrap);
        Local<v8::Function> f = FunctionTemplate::New(isolate, v8_extern_wrapper, wrapper_data)->GetFunction();
        context->Global()->Set(String::NewFromUtf8(isolate, jit_extern.first.c_str()),
                               f);
    }
  }

} // namespace JS_V8;

int run_javascript_v8(const std::string &source, const std::string &fn_name,
                      std::vector<std::pair<Argument, const void *>> args,
                      const std::map<std::string, JITExtern> &externs,
                      JITModule trampolines) {
    using namespace JS_V8;

    debug(0) << "Calling JavaScript function " << fn_name << " with " << args.size() << " args.\n";
    // TODO: thread safety.
    static bool inited = false;
    if (!inited) {
        // Initialize V8.
        V8::InitializeICU();
        Platform* platform = platform::CreateDefaultPlatform();
        V8::InitializePlatform(platform);
        const char *flags[] = {
          "HalideJavaScriptExecutor"
#if 0
          "--trace_concurrent_recompilation=true",
          "--trace_codegen",
          "--always_opt"
#endif
        };
        int flags_size = sizeof(flags)/sizeof(flags[0]);
        V8::SetFlagsFromCommandLine(&flags_size, const_cast<char **>(flags), false);
        V8::Initialize();
        V8::SetArrayBufferAllocator(new HalideArrayBufferAllocator());
        inited = true;
    }

    // Create a new Isolate and make it the current one.
    Isolate* isolate = Isolate::New();

    Isolate::Scope isolate_scope(isolate);

    // Create a stack-allocated handle scope.
    HandleScope handle_scope(isolate);

    // Create a new context.
    Local<Context> context = Context::New(isolate, NULL, make_global_template(isolate));

    // Enter the context for compiling and running the hello world script.
    Context::Scope context_scope(context);

    add_extern_callbacks(isolate, context, externs, trampolines);

    // Create a string containing the JavaScript source code.
    Local<String> source_v8 = String::NewFromUtf8(isolate, source.c_str());

    TryCatch try_catch;
    try_catch.SetCaptureMessage(true);

    // Compile the source code.
    Local<Script> compiled_script = Script::Compile(source_v8);
    if (compiled_script.IsEmpty()) {
        String::Utf8Value error(try_catch.Exception());
        internal_error << "Error compiling JavaScript: " << *error << "\n";
        return -1;
    }
    Local<Value> run_result = compiled_script->Run();
    if (run_result.IsEmpty()) {
        String::Utf8Value error(try_catch.Exception());
        internal_error << "Error running JavaScript: " << *error << "\n";
        return -1;
    }

    debug(0) << "Script compiled.\n";
    Local<v8::Function> function(Local<v8::Function>::Cast(context->Global()->Get(String::NewFromUtf8(isolate, fn_name.c_str()))));

    debug(0) << "Making args.\n";

    std::vector<v8::Handle<Value>> js_args;
    for (size_t i = 0; i < args.size(); i++) {
        Argument &arg(args[i].first);
        if (arg.is_buffer()) {
            js_args.push_back(make_buffer_t(isolate,
                                            (struct buffer_t *)args[i].second,
                                            halide_type_to_external_array_type(arg.type)));
        } else {
            js_args.push_back(wrap_scalar(isolate, arg.type, args[i].second));
        }
    }

    debug(0) << "Calling function.\n";

    // TODO: Is this the correct reciever?
    Local<Value> result = function->Call(function, js_args.size(), &js_args[0]);

    if (result.IsEmpty()) {
        String::Utf8Value error(try_catch.Exception());
        String::Utf8Value message(try_catch.Message()->GetSourceLine());

        internal_error << "Error running JavaScript: " << *error << " | Line: " << *message << "\n";
        return -1;
    }

    return result->Int32Value();
}

}} // close Halide::Internal namespace

#endif

#if WITH_JAVASCRIPT_SPIDERMONKEY

#include "jsapi.h"
#include "jsfriendapi.h"
#include "js/Conversions.h"

namespace Halide { namespace Internal {

namespace JS_SpiderMonkey {

using namespace JS;

void dump_object(const char *label, JSContext *context, HandleObject obj) {
    JSIdArray *ids = JS_Enumerate(context, obj);
    if (ids == NULL) {
      debug(0) << label << ": getting ids failed.\n";
    } else {
      uint32_t len = JS_IdArrayLength(context, ids);
      if (len == 0) {
          debug(0) << label << ": object is empty.\n";
      }
      for (uint32_t i = 0; i < len; i++) {
          jsid cur_id = JS_IdArrayGet(context, ids, i);
          RootedId cur_rooted_id(context, cur_id);
          RootedValue id_val(context);
          JS_IdToValue(context, cur_id, &id_val);

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
      JS_DestroyIdArray(context, ids);
    }
}

// The class of the global object.
static JSClass global_class = {
  "global",
  JSCLASS_GLOBAL_FLAGS,
  JS_PropertyStub,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, NULL, NULL, NULL,
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

enum ExternalArrayType {
  kExternalInt8Array = 1,
  kExternalUint8Array,
  kExternalInt16Array,
  kExternalUint16Array,
  kExternalInt32Array,
  kExternalUint32Array,
  kExternalFloat32Array,
  kExternalFloat64Array,
  kExternalUint8ClampedArray,
};

ExternalArrayType halide_type_to_external_array_type(const Type &t) {
  if (t.is_uint()) {
      switch (t.bits) {
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
       switch (t.bits) {
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
       switch (t.bits) {
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

#if 0
struct WrappedBuffer {
    JSContext *context; // TODO: Can we get this from objects?
    RootedObject buffer;
    RootedObject host_buffer;
    RootedObject min_buffer;
    RootedObject stride_buffer;
    RootedObject extent_buffer;
    RootedObject host_array;
    RootedObject min_array;
    RootedObject stride_array;
    RootedObject extent_array;

    WrappedBuffer(JSContext *context, buffer_t *buf) :
      context(context), buffer(context), host_buffer(context), min_buffer(context), stride_buffer(context), extent_buffer(context),
        host_array(context), min_array(context), stride_array(context), extent_array(context) {
    }

    ~WrappedBuffer() {
        JS_StealArrayBufferContents(context, host_buffer);
        JS_StealArrayBufferContents(context, min_buffer);
        JS_StealArrayBufferContents(context, stride_buffer);
        JS_StealArrayBufferContents(context, extent_buffer);
    }
};
#endif

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
  JS_PropertyStub,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, NULL, NULL, NULL,
  NULL,
  { }
};

Value make_buffer_t(JSContext *context, struct buffer_t *buf, ExternalArrayType element_type) {
    Value result;
    RootedObject buffer(context, JS_NewObject(context, &buffer_t_class));
    JS_SetPrivate(buffer, buf);

    //    debug(0) << "Making host buffer for type " << (int)element_type << " on " << buf << " object is " << &*buffer << " of length " << buffer_total_size(buf) * buf->elem_size << "\n";
    if (buf->host != NULL) {
        RootedObject host_buffer(context, JS_NewArrayBufferWithContents(context, buffer_total_size(buf) * buf->elem_size, buf->host));
        RootedObject host_array(context, make_array_of_type(context, host_buffer, element_type));
        JS_DefineProperty(context, buffer, "host", host_array, JSPROP_READONLY | JSPROP_ENUMERATE);
    } else {
        RootedValue temp_null(context, JSVAL_NULL);
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

    JS_DefineProperty(context, buffer, "dev", 0, JSPROP_ENUMERATE, dev_getter, dev_setter);
    JS_DefineProperty(context, buffer, "elem_size", 0, JSPROP_ENUMERATE, elem_size_getter, elem_size_setter);

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
    debug(0) << "Buffer steal failed.\n";
    return false;
}

static JSClass handle_class = {
  "buffer_T",
  JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL, NULL, NULL, NULL,
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
            switch (t.bits) {
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
            switch (t.bits) {
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
             switch (t.bits) {
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
    event.event = (halide_trace_event_code)temp.toInt32();
    JS_GetProperty(context, js_event, "parent_id", &temp);
    event.parent_id = (halide_trace_event_code)temp.toInt32();
    JS_GetProperty(context, js_event, "type_code", &temp);
    event.type_code = (halide_trace_event_code)temp.toInt32();
    JS_GetProperty(context, js_event, "bits", &temp);
    event.bits = (halide_trace_event_code)temp.toInt32();
    JS_GetProperty(context, js_event, "vector_width", &temp);
    event.vector_width = (halide_trace_event_code)temp.toInt32();
    JS_GetProperty(context, js_event, "value_index", &temp);
    event.value_index = (halide_trace_event_code)temp.toInt32();
    JS_GetProperty(context, js_event, "value", &temp);
    std::unique_ptr<uint8_t> value_storage(make_trace_value(context, temp, event.type_code, event.bits, event.vector_width));
    event.value = (void *)value_storage.get();
    JS_GetProperty(context, js_event, "dimensions", &temp);
    event.dimensions = (halide_trace_event_code)temp.toInt32();

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
  debug(0) << "js_buffer_t_to_struct entered.\n";

    RootedObject buffer_obj(context, &val.toObject());
    
    dump_object("js_buffer_t_to_struct object", context, buffer_obj);

    uint32_t length = 0;
    uint8_t *data = NULL;
    RootedObject array_buffer(context);

    if (get_array_buffer_from_typed_array_field(context, buffer_obj, "host", &array_buffer)) {
        GetArrayBufferLengthAndData(array_buffer, &length, &data);
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
    slot->host_dirty = temp.toBoolean();
    JS_GetProperty(context, buffer_obj, "dev_dirty", &temp);
    slot->dev_dirty = temp.toBoolean();

  debug(0) << "js_buffer_t_to_struct exit.\n";
}

template <typename T>
void val_to_slot(HandleValue val, uint64_t *slot) {
    T js_value = (T)val.toNumber();
    *(T *)slot = js_value;
}

void js_value_to_uint64_slot(const Halide::Type &type, HandleValue val, uint64_t *slot) {
    if (type.is_handle()) {
      debug(0) << "Handle value for argument.\n";
        *(void **)slot = get_user_context(val);
    } else if (type.is_float()) {
        if (type.bits == 32) {
            val_to_slot<float>(val, slot);
        } else {
            internal_assert(type.bits == 64) << "Floating-point type that isn't 32 or 64-bits wide.\n";
            val_to_slot<double>(val, slot);
        }
    } else if (type.is_uint()) {
        if (type.bits == 1) {
            val_to_slot<bool>(val, slot);
        } else if (type.bits == 8) {
            val_to_slot<uint8_t>(val, slot);
        } else if (type.bits == 16) {
            val_to_slot<uint16_t>(val, slot);
        } else if (type.bits == 32) {
            val_to_slot<uint32_t>(val, slot);
        } else if (type.bits == 64) {
            user_error << "Unsigned 64-bit integer types are not supported with JavaScript.\n";
            val_to_slot<uint64_t>(val, slot);
        }
    } else {
        if (type.bits == 1) {
            val_to_slot<bool>(val, slot);
        } else if (type.bits == 8) {
            val_to_slot<int8_t>(val, slot);
        } else if (type.bits == 16) {
            val_to_slot<int16_t>(val, slot);
        } else if (type.bits == 32) {
            val_to_slot<int32_t>(val, slot);
        } else if (type.bits == 64) {
            user_error << "64-bit integer types are not supported with JavaScript.\n";
            val_to_slot<int64_t>(val, slot);
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
  debug(0) << "buffer_t_struct_to_js entered.\n";
    RootedObject buffer_obj(context, &val.toObject());
    
    dump_object("buffer_t_struct_to_js object", context, buffer_obj);
    RootedObject array_buffer(context);

    // If there was host data, this is not a bounds query and results do not need to be copied back.
    if (get_array_buffer_from_typed_array_field(context, buffer_obj, "host", &array_buffer)) {
        return;
    }
  debug(0) << "buffer_t_struct_to_js copying out buffer...\n";

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

  debug(0) << "buffer_t_struct_to_js exited.\n";
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
        if (type.bits == 32) {
            slot_to_return_val<float, double>(slot, val);
        } else {
            internal_assert(type.bits == 64) << "Floating-point type that isn't 32 or 64-bits wide.\n";
            slot_to_return_val<double, double>(slot, val);
        }
    } else if (type.is_uint()) {
        if (type.bits == 1) {
          slot_to_return_val<bool, bool>(slot, val);
        } else if (type.bits == 8) {
          slot_to_return_val<uint8_t, uint32_t>(slot, val);
        } else if (type.bits == 16) {
          slot_to_return_val<uint16_t, uint32_t>(slot, val);
        } else if (type.bits == 32) {
          slot_to_return_val<uint32_t, uint32_t>(slot, val);
        } else if (type.bits == 64) {
            user_error << "Unsigned 64-bit integer types are not supported with JavaScript.\n";
            slot_to_return_val<uint64_t, double>(slot, val);
        }
    } else {
        if (type.bits == 1) {
          slot_to_return_val<bool, bool>(slot, val);
        } else if (type.bits == 8) {
          slot_to_return_val<int8_t, int32_t>(slot, val);
        } else if (type.bits == 16) {
          slot_to_return_val<int16_t, int32_t>(slot, val);
        } else if (type.bits == 32) {
          slot_to_return_val<int32_t, int32_t>(slot, val);
        } else if (type.bits == 64) {
            user_error << "64-bit integer types are not supported with JavaScript.\n";
            slot_to_return_val<int64_t, double>(slot, val);
        }
    }
}

struct CallbackInfo {
    JITExtern jit_extern;
    void (*trampoline)(void **args);
};

bool extern_wrapper(JSContext *context, unsigned argc, JS::Value *vp) {
  debug(0) << "In extern_wrapper with " << argc << " args.\n";
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    RootedObject callee(context, &args.callee());
    
    RootedValue callback_info_holder_val(context);
    JS_GetProperty(context, callee, "trampoline", &callback_info_holder_val);
    RootedObject callback_info_holder(context, &callback_info_holder_val.toObject());

    CallbackInfo *callback_info = (CallbackInfo *)JS_GetPrivate(callback_info_holder);
    JITExtern *jit_extern = &callback_info->jit_extern;

    // Each scalar args is stored in a 64-bit slot
    size_t scalar_args_count = 0;
    // Each buffer_t gets a slot in an array of buffer_t structs
    size_t buffer_t_args_count = 0;

    std::vector<void *> trampoline_args;
    internal_assert(jit_extern->func == NULL);
    for (const ScalarOrBufferT &scalar_or_buffer_t : jit_extern->arg_types) {
        if (scalar_or_buffer_t.is_buffer) {
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
    for (const ScalarOrBufferT &scalar_or_buffer_t : jit_extern->arg_types) {
        if (scalar_or_buffer_t.is_buffer) {
          debug(0) << "Handling buffer arg\n";
            js_buffer_t_to_struct(context, args[args_index++], &buffer_t_args[buffer_t_arg_index]);
            trampoline_args.push_back(&buffer_t_args[buffer_t_arg_index++]);
        } else {
          debug(0) << "Handling arg of type " << scalar_or_buffer_t.scalar_type << "\n";
            js_value_to_uint64_slot(scalar_or_buffer_t.scalar_type, args[args_index++], &scalar_args[scalar_arg_index]);
            trampoline_args.push_back(&scalar_args[scalar_arg_index++]);
        }
    }

    uint64_t ret_val;
    if (!jit_extern->is_void_return) {
        trampoline_args.push_back(&ret_val);
    }
    debug(0) << "Calling extern_wrapper trampoline.\n";
    (*callback_info->trampoline)(&trampoline_args[0]);
    debug(0) << "Returning from extern_wrapper trampoline.\n";

    args_index = 0;
    buffer_t_arg_index = 0;
    for (const ScalarOrBufferT &scalar_or_buffer_t : jit_extern->arg_types) {
        if (scalar_or_buffer_t.is_buffer) {
            buffer_t_struct_to_js(context, &buffer_t_args[buffer_t_arg_index++], args[args_index]);
        }
        args_index++;
        // No need to retrieve scalar args as they are passed by value.
    }
    
    if (!jit_extern->is_void_return) {
        uint64_slot_to_return_value(jit_extern->ret_type, &ret_val, args.rval());
    }

    debug(0) << "Leaving extern_wrapper.\n";
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
                          const std::map<std::string, JITExtern> &externs,
                          JITModule trampolines,
                          std::vector<CallbackInfo> &callback_storage) {
    size_t storage_index = 0;
    for (const std::pair<std::string, JITExtern> &jit_extern : externs) {
        JSFunction *f = JS_DefineFunction(context, global, jit_extern.first.c_str(), &extern_wrapper, 0, 0);
        if (f == NULL) {
            return false;
        }
        callback_storage[storage_index].jit_extern = jit_extern.second;
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

} // namespace JS_SpiderMonkey

int run_javascript_spidermonkey(const std::string &source, const std::string &fn_name,
                                std::vector<std::pair<Argument, const void *>> args,
                                const std::map<std::string, JITExtern> &externs,
                                JITModule trampolines) {
    int32_t result = 0; 

    using namespace JS_SpiderMonkey;

    debug(0) << "Calling JavaScript function " << fn_name << " with " << args.size() << " args.\n";
    // TODO: thread safety.
    static bool inited = false;
    if (!inited) {
        if (!JS_Init()) {
            return -1;
        }
        inited = true;
    }

    // Create a JS runtime.
    JSRuntime *runtime = JS_NewRuntime(128L * 1024L * 1024L);
    if (!runtime) {
        return -1;
    }

    // Create a context.
    JSContext *context = JS_NewContext(runtime, 8192);
    if (!context) {
        JS_DestroyRuntime(runtime);
        return -1;
    }
    JS_SetErrorReporter(runtime, reportError);

    {
        JSAutoRequest auto_request(context);

        // Create the global object and a new compartment.
        RootedObject global(context);
        global = JS_NewGlobalObject(context, &global_class, NULL,
                                    JS::DontFireOnNewGlobalHook);
        if (!global) {
            JS_DestroyContext(context);
            JS_DestroyRuntime(runtime);
            return -1;
        }

        JSAutoCompartment auto_compartment(context, global);

        // Populate the global object with the standard globals, like Object and
        // Array.
        if (!JS_InitStandardClasses(context, global)) {
            JS_DestroyContext(context);
            JS_DestroyRuntime(runtime);
            return -1;
        }

        make_callbacks(context, global);

        std::vector<CallbackInfo> callback_info_storage(externs.size());
        if (!add_extern_callbacks(context, global, externs, trampolines, callback_info_storage)) {
            debug(0) << "Failure adding extern callbacks to SpiderMonkey globals.\n";
            JS_DestroyContext(context);
            JS_DestroyRuntime(runtime);
            return -1;
        }

        RootedValue script_result(context);
        CompileOptions options(context);
        bool succeeded = JS::Evaluate(context, global, options, source.data(), source.size(), &script_result);
        if (!succeeded) {
            debug(0) << "JavaScript script evaulation failed(SpiderMonkey).\n";
            JS_DestroyContext(context);
            JS_DestroyRuntime(runtime);
            return -1;
        }

        debug(0) << "Script compiled(SpiderMonkey).\n";

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
        succeeded = JS::Call(context, global, fn_name.c_str(), js_args, &js_result);

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
    }

    JS_DestroyContext(context);
    
    return result;
}

}} // close Halide::Internal namespace

#endif

namespace Halide { namespace Internal {

int run_javascript(const Target &target, const std::string &source, const std::string &fn_name,
                   const std::vector<std::pair<Argument, const void *>> &args,
                   const std::map<std::string, JITExtern> &externs,
                   const std::vector<JITModule> &extern_deps) {
#if !defined(WITH_JAVASCRIPT_V8) && !defined(WITH_JAVASCRIPT_SPIDERMONKEY)
    user_error << "Cannot run JITted JavaScript without configuring a JavaScript engine.";
    return -1;
#endif

    // This call explicitly does not use "_argv" as the suffix because
    // that name may already exist and if so, will return an int
    // instead of taking a pointer at the end of the args list to
    // receive the result value.
    JITModule trampolines = JITModule::make_trampolines_module(target, externs, "_js_trampoline", extern_deps);

#ifdef WITH_JAVASCRIPT_V8
    if (!target.has_feature(Target::JavaScript_SpiderMonkey)) {
      return run_javascript_v8(source, fn_name, args, externs, trampolines);
    }
#else    
    if (target.has_feature(Target::JavaScript_V8)) {
        user_error << "V8 JavaScript requrested without configuring V8 JavaScript engine.";
    }
#endif

#ifdef WITH_JAVASCRIPT_SPIDERMONKEY
    return run_javascript_spidermonkey(source, fn_name, args, externs, trampolines);
#else    
    if (target.has_feature(Target::JavaScript_SpiderMonkey)) {
        user_error << "V8 JavaScript requrested without configuring V8 JavaScript engine.";
    }
#endif



}

}} // close Halide::Internal namespace
