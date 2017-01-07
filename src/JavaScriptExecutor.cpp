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

}

#endif

namespace Halide { namespace Internal {

// TODO: Filter math routines, runtime routines, etc.
std::map<std::string, Halide::JITExtern> filter_externs(const std::map<std::string, Halide::JITExtern> &externs) {
    std::map<std::string, Halide::JITExtern> result = externs;
    result.erase("halide_print");
    return result;
}

}} // close Halide::Internal namespace

#if WITH_JAVASCRIPT_V8

#include "v8.h"
#include "libplatform/libplatform.h"

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
    //    String::Utf8Value str_val(property);
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
        host_getter = get_host_array<Int8Array>;
        break;
    case kExternalUint8Array:
        host_getter = get_host_array<Uint8Array>;
        break;
    case kExternalInt16Array:
        host_getter = get_host_array<Int16Array>;
        break;
    case kExternalUint16Array:
        host_getter = get_host_array<Uint16Array>;
        break;
    case kExternalInt32Array:
        host_getter = get_host_array<Int32Array>;
        break;
    case kExternalUint32Array:
        host_getter = get_host_array<Uint32Array>;
        break;
    case kExternalFloat32Array:
        host_getter = get_host_array<Float32Array>;
        break;
    case kExternalFloat64Array:
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
    Local<ObjectTemplate> object_template = make_buffer_t_template(isolate, element_type);
    Local<Object> wrapper = object_template->NewInstance();
    Local<External> buf_wrap(External::New(isolate, buf));
    wrapper->SetInternalField(0, buf_wrap);
    return wrapper;
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
            switch (t.bits()) {
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
            switch (t.bits()) {
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
             switch (t.bits()) {
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

// Useful for debugging...
#if 0
void dump_object(Local<Object> &obj, bool include_values = false) {
    Local<Array> names = obj->GetPropertyNames();

    debug(0) << "Dumping object names for obj with " << names->Length() << " names.\n";
    for (uint32_t i = 0; i < names->Length(); i++) {
        Local<Value> name = names->Get(i);
        //        internal_assert(name->IsString()) << "Name is not a string.\n";
        String::Utf8Value printable(name);
        if (include_values) {
            Local<Value> val = obj->Get(name);
            if (val->IsObject()) {
                Local<Object> sub_obj = val->ToObject();
                debug(0) << "Dumping subobject under key: " << *printable << "\n";
                dump_object(sub_obj, true);
            } else {
                String::Utf8Value val_printable(val);
                debug(0) << "object key " << *printable << " has value: " << *val_printable << "\n";
            }
        } else {
            debug(0) << "object has key: " << *printable << "\n";
        }
    }
}
#endif

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
    Local<Value> arg = args[1];
    String::Utf8Value value(arg);

    // Turns out to be convenient to get debug output in some cases where the
    // user_context is not setup. 
    if (args[0]->IsNull()) {
        debug(0) << "Bad user_context to print_callback: " << *value;
        return;
    }

    Local<Object> user_context = args[0]->ToObject();
    Local<External> handle_wrapper = Local<External>::Cast(user_context->GetInternalField(0));
    JITUserContext *jit_user_context = (JITUserContext *)handle_wrapper->Value();

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
    Local<Value> arg = args[1];
    String::Utf8Value value(arg);

    // Turns out to be convenient to get debug output in some cases where the
    // user_context is not setup. 
    if (args[0]->IsNull()) {
        halide_runtime_error << "Bad user_context to error_callback: " << *value;
        return;
    }

    Local<Object> user_context = args[0]->ToObject();
    Local<External> handle_wrapper = Local<External>::Cast(user_context->GetInternalField(0));
    JITUserContext *jit_user_context = (JITUserContext *)handle_wrapper->Value();

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
    event.type.code = (halide_type_code_t)js_event->Get(String::NewFromUtf8(isolate, "type_code"))->Int32Value();
    event.type.bits = js_event->Get(String::NewFromUtf8(isolate, "bits"))->Int32Value();
    event.type.lanes = js_event->Get(String::NewFromUtf8(isolate, "vector_width"))->Int32Value();
    event.value_index = js_event->Get(String::NewFromUtf8(isolate, "value_index"))->Int32Value();
    std::unique_ptr<uint8_t> value_storage(make_trace_value(js_event->Get(String::NewFromUtf8(isolate, "value"))->ToObject(), event.type.code, event.type.bits, event.type.lanes));
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

int compile_function(Isolate *isolate, Local<Context> &context,
                     const std::string &fn_name, const std::string &source,
                     Local<v8::Function> &result) {
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

    result = Local<v8::Function>::Cast(context->Global()->Get(String::NewFromUtf8(isolate, fn_name.c_str())));
    return 0;
}

int make_js_copy_routine(Isolate *isolate, Local<Context> &context,
                         const buffer_t *buf, const Type &type, int32_t dimensions,
                         Local<v8::Function> &result) {
    std::stringstream type_name_stream;
    type_name_stream << type;
    std::string fn_name = "halide_copy_buffer_" + type_name_stream.str() + "_" + std::to_string(dimensions) + "_dimensions";

    Local<Value> preexisting = context->Global()->Get(String::NewFromUtf8(isolate, fn_name.c_str()));
    if (preexisting->IsFunction()) {
        result = Local<v8::Function>::Cast(preexisting);
        return 0;
    }

    // Chunk of Halide code to copy input to output
    ImageParam in(type, dimensions);
    Func out;
    out(_) = in(_);
    in.dim(0).set_stride(Expr());
    out.output_buffer().dim(0).set_stride(Expr());

    Target temp_target;
    temp_target.set_features({Target::JavaScript, Target::NoRuntime});
    Module module = out.compile_to_module({ in }, fn_name, temp_target);
    std::stringstream js_out_stream;
    Internal::CodeGen_JavaScript cg(js_out_stream);
    cg.compile(module);

    debug(0) << js_out_stream.str() << "\n";

    return compile_function(isolate, context, fn_name, js_out_stream.str(), result);
}

/* This routine copies a JS object representing a buffer_t to a
 * buffer_t structure that can be passed to a C routine. It allocates
 * storage for the target buffer.
 *
 * With V8, it is impossible to get a pointer to the underly storage
 * for an array object without forcing it to be an external array,
 * thus getting a direct pointer mutates the state of the object. The
 * arrays used can be any sort of array like object from a variety of
 * sources, and thus it is not acceptable change the object to be
 * external. In some cases, the array is already external and this
 * could be optimized, but since this is only used for testing the
 * performance hit of copying the buffers is not a huge concern.
 *
 * The copy is done using Halide generated JS code to handle any sort
 * of array that Halide can handle. (Using e.g. ArrayBuffer.slice()
 * would impose a cosntraint that the vlaue is an ArrayBuffer based
 * thing, etc.) This is likely not important right now, but it results
 * on concise code and better exercises the JavaScript codegen, which
 * improves testing anyway.
 */
int32_t js_buffer_t_to_struct(Isolate *isolate, const Local<Value> &val, struct buffer_t *slot) {
    Local<Object> buf = val->ToObject();
    Local<Context> context = buf->CreationContext();

    Local<Object> extents = buf->Get(String::NewFromUtf8(isolate, "extent"))->ToObject();
    Local<Object> mins = buf->Get(String::NewFromUtf8(isolate, "min"))->ToObject();
    Local<Object> strides = buf->Get(String::NewFromUtf8(isolate, "stride"))->ToObject();
    for (int32_t i = 0; i < 4; i++) {
        slot->extent[i] = extents->Has(i) ? extents->Get(i)->Int32Value() : 0;
        slot->min[i] = mins->Has(i) ? mins->Get(i)->Int32Value() : 0;
        slot->stride[i] = strides->Has(i) ? strides->Get(i)->Int32Value() : 0;
    }
    slot->dev = 0;
    slot->elem_size = buf->Get(String::NewFromUtf8(isolate, "elem_size"))->Int32Value();
    slot->host_dirty = buf->Get(String::NewFromUtf8(isolate, "host_dirty"))->BooleanValue();
    slot->dev_dirty = buf->Get(String::NewFromUtf8(isolate, "dev_dirty"))->BooleanValue();

    Type buf_type_guess;
    Local<Value> host_array = buf->Get(String::NewFromUtf8(isolate, "host"));
    if (host_array->IsInt8Array()) {
        buf_type_guess = Int(8);
    } else if (host_array->IsUint8Array()) {
        buf_type_guess = UInt(8);
    } else if (host_array->IsInt16Array()) {
        buf_type_guess = Int(16);
    } else if (host_array->IsUint16Array()) {
        buf_type_guess = UInt(16);
    } else if (host_array->IsInt32Array()) {
        buf_type_guess = Int(32);
    } else if (host_array->IsUint32Array()) {
        buf_type_guess = UInt(32);
    } else if (host_array->IsFloat32Array()) {
        buf_type_guess = Float(32);
    } else if (host_array->IsFloat64Array()) {
        buf_type_guess = Float(64);
    } else {
        if (slot->elem_size == 8) {
            buf_type_guess = Float(64);
        } else {
            buf_type_guess = UInt(slot->elem_size * 8);
        }
    }

    int32_t dimensions = 0;
    while (dimensions < 4 && slot->extent[dimensions] != 0) {
        dimensions++;
    }

    int32_t result = 0;
    if (!host_array->IsNull() && dimensions != 0) {
        Local<v8::Function> copy_function;
        if (make_js_copy_routine(isolate, context, slot, buf_type_guess, dimensions, copy_function) != 0) {
            return -1;
        }

        int32_t total_size = buffer_total_size(slot);
        slot->host = (uint8_t *)malloc(total_size * slot->elem_size);

        Local<Object> temp_buf = make_buffer_t(isolate, slot, halide_type_to_external_array_type(buf_type_guess));

        v8::Handle<Value> js_args[2];
        js_args[0] = val;
        js_args[1] = temp_buf;

        result = copy_function->Call(copy_function, 2, &js_args[0])->Int32Value();

        if (result != 0) {
            free(slot->host);
            slot->host = NULL;
        }
    } else {
        slot->host = nullptr;
    }

    return result;
}

template <typename T>
void val_to_slot(const Local<Value> &val, uint64_t *slot) {
    T js_value = (T)val->NumberValue();
    *(T *)slot = js_value;
}

void js_value_to_uint64_slot(const Halide::Type &type, const Local<Value> &val, uint64_t *slot) {
  //    String::Utf8Value printable(val);
  //  debug(0) << "Argument is " << *printable << "\n";
    if (type.is_handle()) {
        Local<Object> wrapper_obj = val->ToObject();
        Local<External> wrapped_handle = Local<External>::Cast(wrapper_obj->GetInternalField(0));
        *slot = (uint64_t)wrapped_handle->Value();
    } else if (type.is_float()) {
        if (type.bits() == 32) {
            val_to_slot<float>(val, slot);
        } else {
            internal_assert(type.bits() == 64) << "Floating-point type that isn't 32 or 64-bits wide.\n";
            val_to_slot<double>(val, slot);
        }
    } else if (type.is_uint()) {
        if (type.bits() == 1) {
            val_to_slot<bool>(val, slot);
        } else if (type.bits() == 8) {
            val_to_slot<uint8_t>(val, slot);
        } else if (type.bits() == 16) {
            val_to_slot<uint16_t>(val, slot);
        } else if (type.bits() == 32) {
            val_to_slot<uint32_t>(val, slot);
        } else if (type.bits() == 64) {
            user_error << "Unsigned 64-bit integer types are not supported with JavaScript.\n";
            val_to_slot<uint64_t>(val, slot);
        }
    } else {
        if (type.bits() == 1) {
            val_to_slot<bool>(val, slot);
        } else if (type.bits() == 8) {
            val_to_slot<int8_t>(val, slot);
        } else if (type.bits() == 16) {
            val_to_slot<int16_t>(val, slot);
        } else if (type.bits() == 32) {
            val_to_slot<int32_t>(val, slot);
        } else if (type.bits() == 64) {
            user_error << "64-bit integer types are not supported with JavaScript.\n";
            val_to_slot<int64_t>(val, slot);
        }
    }
    //    debug(0) << "Slot is " << *slot << "(or " << *(float *)slot << ")\n";
}

/* This routine copies a buffer_t struct to the storage pointed to by
 * a JS object representing a buffer_t. It frees the storage for the
 * source buffer. */
int buffer_t_struct_to_js(Isolate *isolate, buffer_t *slot, Local<Value> val) {
    Local<Object> buf = val->ToObject();
    Local<Context> context = buf->CreationContext();

    int32_t dimensions = 0;
    while (dimensions < 4 && slot->extent[dimensions] != 0) {
        dimensions++;
    }

    Local<Object> extents = buf->Get(String::NewFromUtf8(isolate, "extent"))->ToObject();
    Local<Object> mins = buf->Get(String::NewFromUtf8(isolate, "min"))->ToObject();
    Local<Object> strides = buf->Get(String::NewFromUtf8(isolate, "stride"))->ToObject();
    for (int32_t i = 0; i < 4; i++) {
        if (i < dimensions || extents->Has(i)) {
            Local<Value> extent = Integer::New(isolate, slot->extent[i]);
            extents->Set(i, extent);
        }
        if (i < dimensions || mins->Has(i)) {
            Local<Value> min = Integer::New(isolate, slot->min[i]);
            mins->Set(i, min);
        }
        if (i < dimensions || strides->Has(i)) {
            Local<Value> stride = Integer::New(isolate, slot->stride[i]);
            strides->Set(i, stride);
        }
    }
    Local<Value> elem_size = Integer::New(isolate, slot->elem_size);
    buf->Set(String::NewFromUtf8(isolate, "elem_size"), elem_size);
    buf->Set(String::NewFromUtf8(isolate, "host_dirty"), Boolean::New(isolate, slot->host_dirty));
    buf->Set(String::NewFromUtf8(isolate, "dev_dirty"), Boolean::New(isolate, slot->dev_dirty));

    Type buf_type_guess;
    Local<Value> host_array = buf->Get(String::NewFromUtf8(isolate, "host"));
    if (host_array->IsInt8Array()) {
        buf_type_guess = Int(8);
    } else if (host_array->IsUint8Array()) {
        buf_type_guess = UInt(8);
    } else if (host_array->IsInt16Array()) {
        buf_type_guess = Int(16);
    } else if (host_array->IsUint16Array()) {
        buf_type_guess = UInt(16);
    } else if (host_array->IsInt32Array()) {
        buf_type_guess = Int(32);
    } else if (host_array->IsUint32Array()) {
        buf_type_guess = UInt(32);
    } else if (host_array->IsFloat32Array()) {
        buf_type_guess = Float(32);
    } else if (host_array->IsFloat64Array()) {
        buf_type_guess = Float(64);
    } else {
        if (slot->elem_size == 8) {
            buf_type_guess = Float(64);
        } else {
            buf_type_guess = UInt(slot->elem_size * 8);
        }
    }

    int32_t result = 0;
    if (!host_array->IsNull() && dimensions != 0) {
        Local<v8::Function> copy_function;
        if (make_js_copy_routine(isolate, context, slot, buf_type_guess, dimensions, copy_function) != 0) {
            return -1;
        }

        Local<Object> temp_buf = make_buffer_t(isolate, slot, halide_type_to_external_array_type(buf_type_guess));

        v8::Handle<Value> js_args[2];
        js_args[0] = temp_buf;
        js_args[1] = val;

        // TODO: Is this the correct reciever?
        result = copy_function->Call(copy_function, 2, &js_args[0])->Int32Value();

        free(slot->host);
        slot->host = NULL;
    } else {
        internal_assert(slot->host == nullptr) << "";
    }

    return result;
}

template <typename T, typename S>
void slot_to_return_val(const uint64_t *slot, ReturnValue<Value> val) {
    T slot_value = *(T *)slot;
    val.Set((S)slot_value);
}

void uint64_slot_to_return_value(const Halide::Type &type, const uint64_t *slot, ReturnValue<Value> val) {
    if (type.is_handle()) {
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
    for (const Type &arg_type : iter->second.extern_c_function().signature().arg_types()) {
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
    for (const Type &arg_type : iter->second.extern_c_function().signature().arg_types()) {
        if (arg_type == type_of<struct buffer_t *>()) {
            js_buffer_t_to_struct(isolate, args[args_index++], &buffer_t_args[buffer_t_arg_index]);
            trampoline_args.push_back(&buffer_t_args[buffer_t_arg_index++]);
        } else {
            js_value_to_uint64_slot(arg_type, args[args_index++], &scalar_args[scalar_arg_index]);
            trampoline_args.push_back(&scalar_args[scalar_arg_index++]);
        }
    }

    uint64_t ret_val;
    if (!iter->second.extern_c_function().signature().is_void_return()) {
        trampoline_args.push_back(&ret_val);
    }
    (*trampoline)(&trampoline_args[0]);

    args_index = 0;
    buffer_t_arg_index = 0;
    for (const Type &arg_type : iter->second.extern_c_function().signature().arg_types()) {
        if (arg_type == type_of<struct buffer_t *>()) {
            buffer_t_struct_to_js(isolate, &buffer_t_args[buffer_t_arg_index++], args[args_index++]);
        } else {
            args_index++;
        }
        // No need to retrieve scalar args as they are passed by value.
    }
    
    if (!iter->second.extern_c_function().signature().is_void_return()) {
        uint64_slot_to_return_value(iter->second.extern_c_function().signature().ret_type(), &ret_val, args.GetReturnValue());
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

int compile_javascript_v8(const std::string &source, const std::string &fn_name,
                          const std::map<std::string, JITExtern> &externs,
                          JITModule trampolines,
                          JS_V8::Isolate *&isolate, JS_V8::Persistent<JS_V8::Context> &context_holder,
                          JS_V8::Persistent<JS_V8::Function> &function_holder) {
    using namespace JS_V8;

    debug(0) << "Compiling JavaScript function " << fn_name << "\n";
    // TODO: thread safety.
    static std::unique_ptr<ArrayBuffer::Allocator> array_buffer_allocator(new HalideArrayBufferAllocator());
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
        inited = true;
    }

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

    TryCatch try_catch;
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

    TryCatch try_catch;
    try_catch.SetCaptureMessage(true);

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

    Local<v8::Function> function = Local<v8::Function>::New(isolate, function_holder);
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
    event.event = (halide_trace_event_code)temp.toInt32();
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
            val_to_slot<float>(val, slot);
        } else {
            internal_assert(type.bits() == 64) << "Floating-point type that isn't 32 or 64-bits wide.\n";
            val_to_slot<double>(val, slot);
        }
    } else if (type.is_uint()) {
        if (type.bits() == 1) {
            val_to_slot<bool>(val, slot);
        } else if (type.bits() == 8) {
            val_to_slot<uint8_t>(val, slot);
        } else if (type.bits() == 16) {
            val_to_slot<uint16_t>(val, slot);
        } else if (type.bits() == 32) {
            val_to_slot<uint32_t>(val, slot);
        } else if (type.bits() == 64) {
            user_error << "Unsigned 64-bit integer types are not supported with JavaScript.\n";
            val_to_slot<uint64_t>(val, slot);
        }
    } else {
        if (type.bits() == 1) {
            val_to_slot<bool>(val, slot);
        } else if (type.bits() == 8) {
            val_to_slot<int8_t>(val, slot);
        } else if (type.bits() == 16) {
            val_to_slot<int16_t>(val, slot);
        } else if (type.bits() == 32) {
            val_to_slot<int32_t>(val, slot);
        } else if (type.bits() == 64) {
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
                          const std::map<std::string, JITExtern> &externs,
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
                                     const std::map<std::string, JITExtern> &externs,
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

    std::map<std::string, JITExtern> externs;
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
        if (v8_isolate != NULL) {
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
EXPORT RefCount &ref_count<JavaScriptModuleContents>(const JavaScriptModuleContents *p) {
    return p->ref_count;
}

template<>
EXPORT void destroy<JavaScriptModuleContents>(const JavaScriptModuleContents *p) {
    delete p;
}

EXPORT JavaScriptModule compile_javascript(const Target &target, const std::string &source, const std::string &fn_name,
                                           const std::map<std::string, JITExtern> &externs,
                                           const std::vector<JITModule> &extern_deps) {
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
                                  module.contents->v8_function) ==0) {
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
EXPORT int run_javascript(JavaScriptModule module, const std::vector<std::pair<Argument, const void *>> &args) {
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
