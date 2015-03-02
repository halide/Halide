#include "Error.h"
#include "JavaScriptExecutor.h"

#include "buffer_t.h"
#include <vector>

#if !WITH_JAVASCRIPT_V8
namespace Halide { namespace Internal {

int run_javascript(const std::string &source, const std::string &fn_name, std::vector<Type> arg_types, std::vector<void *> args) {
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
void get_host_array(Local<String> /* property */,
                    const PropertyCallbackInfo<Value> &info) {
    Local<Object> obj = info.Holder();
    Local<External> buf_wrapper = Local<External>::Cast(obj->GetInternalField(0));
    const buffer_t *buf = (const buffer_t *)buf_wrapper->Value();
    int32_t total_size = 1;
    for (int i = 0; i < 4; i++) {
        int32_t stride = buf->stride[i];
        if (stride < 0) stride = -stride;
        if ((buf->extent[i] * stride) > total_size) {
            total_size = buf->extent[i] * stride;
        }
    }
    Local<ArrayBuffer> array_buf = ArrayBuffer::New(info.GetIsolate(), total_size * buf->elem_size);
    Local<T> value = T::New(array_buf, 0, total_size);
    info.GetReturnValue().Set(value);  
}

template <typename T>
void get_struct_field(Local<String> /* property */,
                      const PropertyCallbackInfo<Value> &info) {
    Local<Object> obj = info.Holder();
    Local<External> buf_wrapper = Local<External>::Cast(obj->GetInternalField(0));
    const void *buf = buf_wrapper->Value();
    int32_t offset = info.Data()->Uint32Value();
    T value = *(const T *)((const char *)buf + offset);
    info.GetReturnValue().Set(value);  
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

void get_buffer_t_array_field(Local<String> /* property */,
                              const PropertyCallbackInfo<Value> &info) {
    Local<Object> obj = info.Holder();
    Local<External> buf_wrapper = Local<External>::Cast(obj->GetInternalField(0));
    const void *buf = buf_wrapper->Value();
    int32_t offset = info.Data()->Uint32Value();
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
#if 0
    object_template->SetAccessor(String::NewFromUtf8(isolate, "dev"),
                                 get_struct_field<void *>, set_struct_field<void *>,
                                 Integer::New(isolate, offsetof(buffer_t, dev)));
#endif
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

Local<Value> wrap_scalar(Isolate *isolate, const Parameter &p) {
    double val = 0;
    if (p.type().is_uint()) {
        switch (p.type().bits) {
            case 8:
                val = p.get_scalar<uint8_t>();
                break;
            case 16:
                val = p.get_scalar<uint16_t>();
                break;
            case 32:
                val = p.get_scalar<uint32_t>();
                break;
            default:
                internal_error << "Unsupported bit size.\n";
                val = p.get_scalar<uint8_t>();
                break;
        }
    } else if (p.type().is_int()) {
        switch (p.type().bits) {
            case 8:
                val = p.get_scalar<int8_t>();
                break;
            case 16:
                val = p.get_scalar<int16_t>();
                break;
            case 32:
                val = p.get_scalar<int32_t>();
                break;
            default:
                internal_error << "Unsupported bit size.\n";
                val = p.get_scalar<int8_t>();
                break;
        }
     } else   if (p.type().is_float()) {
         switch (p.type().bits) {
             case 32:
                 val = p.get_scalar<float>();
                 break;
             case 64:
                 val = p.get_scalar<double>();
                 break;
             default:
                 internal_error << "Unsupported bit size.\n";
                 val = p.get_scalar<float>();
                 break;
         }
     }

    return Number::New(isolate, val);
}

int run_javascript(const std::string &source, const std::string &fn_name, std::vector<Parameter> parameters) {
    // Initialize V8.
    V8::InitializeICU();
    Platform* platform = platform::CreateDefaultPlatform();
    V8::InitializePlatform(platform);
    V8::Initialize();

    // Create a new Isolate and make it the current one.
    Isolate* isolate = Isolate::New();

    Isolate::Scope isolate_scope(isolate);

    // Create a stack-allocated handle scope.
    HandleScope handle_scope(isolate);

    // Create a new context.
    Local<Context> context = Context::New(isolate);

    // Enter the context for compiling and running the hello world script.
    Context::Scope context_scope(context);

    // Create a string containing the JavaScript source code.
    Local<String> source_v8 = String::NewFromUtf8(isolate, source.c_str());

    // Compile the source code.
    Script::Compile(source_v8);

    Local<Object> function = context->Global()->Get(String::NewFromUtf8(isolate, fn_name.c_str()))->ToObject();

    std::vector<v8::Handle<Value>> js_args;
    for (size_t i = 0; i < parameters.size(); i++) {
        Parameter &p(parameters[i]);
        if (p.is_buffer()) {
            js_args.push_back(make_buffer_t(isolate,
                                            p.get_buffer().raw_buffer(),
                                            halide_type_to_external_array_type(p.type())));
        } else {
            js_args.push_back(wrap_scalar(isolate, p));
        }
    }

    // TODO: Is this the correct reciever?
    Local<Value> result = function->CallAsFunction(function, js_args.size(), &js_args[0]);

    return result->Int32Value();
}

}} // close Halide::Internal namespace

#endif
