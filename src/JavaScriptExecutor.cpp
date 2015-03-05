#include "Error.h"
#include "JavaScriptExecutor.h"
#include "JITModule.h"

#include "runtime/HalideRuntime.h"
#include <vector>

#if !WITH_JAVASCRIPT_V8
namespace Halide { namespace Internal {

int run_javascript(const std::string &source, const std::string &fn_name, std::vector<Type> arg_types,
                   std::vector<std::pair<Argument, const void *> >args) {
    user_error << "Cannot run JITted JavaScript without configuring a JavaScript engine.";
    return -1;
}

}} // close Halide::Internal namespace

#else

#include "include/v8.h"
#include "include/libplatform/libplatform.h"

namespace Halide { namespace Internal {

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
        int32_t total_size = 1;
        for (int i = 0; i < 4; i++) {
            int32_t stride = buf->stride[i];
            if (stride < 0) stride = -stride;
            if ((buf->extent[i] * stride) > total_size) {
                total_size = buf->extent[i] * stride;
            }
        }
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
#if 0
  /** Allocates a new string from UTF-8 data.*/
  static Local<String> NewFromUtf8(Isolate* isolate,
                                  const char* data,
                                  ingType type = kNormalString,
                                  int length = -1);
#endif

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
  //  debug(0) << "Making buffer_t on " << buf << " which has elem_size " << buf->elem_size << "\n";
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
    internal_assert(args.Length() >= 2) << "Not enough arguments to error_callback in JavaScriptExecutor.\n";
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

int run_javascript(const std::string &source, const std::string &fn_name,
                   std::vector<std::pair<Argument, const void *> > args) {
    debug(0) << "Calling JavaScript function " << fn_name << " with " << args.size() << " args.\n";
    // TODO: thread safety.
    static bool inited = false;
    if (!inited) {
        // Initialize V8.
        V8::InitializeICU();
        Platform* platform = platform::CreateDefaultPlatform();
        V8::InitializePlatform(platform);
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
