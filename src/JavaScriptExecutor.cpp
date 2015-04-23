#include "Error.h"
#include "JavaScriptExecutor.h"
#include "JITModule.h"
#include "Target.h"

#include "runtime/HalideRuntime.h"
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

Local<ObjectTemplate> make_callbacks(Isolate *isolate) {
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

} // namespace JS_V8;

int run_javascript_v8(const std::string &source, const std::string &fn_name,
                      std::vector<std::pair<Argument, const void *> > args) {
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
    Local<Context> context = Context::New(isolate, NULL, make_callbacks(isolate));

    // Enter the context for compiling and running the hello world script.
    Context::Scope context_scope(context);

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

Value make_buffer_t(JSContext *context, struct buffer_t *buf, ExternalArrayType element_type) {
    Value result;
    RootedObject buffer(context, JS_NewObject(context, &buffer_t_class));
    JS_SetPrivate(buffer, buf);

    //    debug(0) << "Making host buffer for type " << (int)element_type << " on " << buf << " object is " << &*buffer << " of length " << buffer_total_size(buf) * buf->elem_size << "\n";
    if (buf->host != NULL) {
        RootedObject host_buffer(context, JS_NewArrayBufferWithContents(context, buffer_total_size(buf) * buf->elem_size, buf->host));
        RootedObject host_array(context, make_array_of_type(context, host_buffer, element_type));
        JS_DefineProperty(context, buffer, "host", host_array, JSPROP_READONLY);
    } else {
        RootedValue temp_null(context, JSVAL_NULL);
        JS_DefineProperty(context, buffer, "host", temp_null, JSPROP_READONLY);
    }

    //    debug(0) << "Making min buffer of length " << sizeof(buf->min) << "\n";
    RootedObject min_buffer(context, JS_NewArrayBufferWithContents(context, sizeof(buf->min), &buf->min[0]));
    RootedObject min_array(context, JS_NewInt32ArrayWithBuffer(context, min_buffer, 0, -1));
    JS_DefineProperty(context, buffer, "min", min_array, JSPROP_READONLY);

    RootedObject stride_buffer(context, JS_NewArrayBufferWithContents(context, sizeof(buf->stride), &buf->stride[0]));
    RootedObject stride_array(context, JS_NewInt32ArrayWithBuffer(context, stride_buffer, 0, -1));
    JS_DefineProperty(context, buffer, "stride", stride_array, JSPROP_READONLY);

    RootedObject extent_buffer(context, JS_NewArrayBufferWithContents(context, sizeof(buf->extent), &buf->extent[0]));
    RootedObject extent_array(context, JS_NewInt32ArrayWithBuffer(context, extent_buffer, 0, -1));
    JS_DefineProperty(context, buffer, "extent", extent_array, JSPROP_READONLY);

    JS_DefineProperty(context, buffer, "dev", 0, 0, dev_getter, dev_setter);
    JS_DefineProperty(context, buffer, "elem_size", 0, 0, elem_size_getter, elem_size_setter);

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
    RootedString arg_str(context, args[1].toString());
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
    RootedString arg_str(context, args[1].toString());
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
    RootedString func_str(context, temp.toString());
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

} // namespace JS_SpiderMonkey

int run_javascript_spidermonkey(const std::string &source, const std::string &fn_name,
                                std::vector<std::pair<Argument, const void *> > args) {
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
                   const std::vector<std::pair<Argument, const void *> > &args) {
#if !defined(WITH_JAVASCRIPT_V8) && !defined(WITH_JAVASCRIPT_SPIDERMONKEY)
    user_error << "Cannot run JITted JavaScript without configuring a JavaScript engine.";
    return -1;
#endif

#ifdef WITH_JAVASCRIPT_V8
    if (!target.has_feature(Target::JavaScript_SpiderMonkey)) {
        return run_javascript_v8(source, fn_name, args);
    }
#else    
    if (target.has_feature(Target::JavaScript_V8)) {
        user_error << "V8 JavaScript requrested without configuring V8 JavaScript engine.";
    }
#endif

#ifdef WITH_JAVASCRIPT_SPIDERMONKEY
    return run_javascript_spidermonkey(source, fn_name, args);
#else    
    if (target.has_feature(Target::JavaScript_SpiderMonkey)) {
        user_error << "V8 JavaScript requrested without configuring V8 JavaScript engine.";
    }
#endif



}

}} // close Halide::Internal namespace
