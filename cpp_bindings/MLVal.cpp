#include "mlval.h"

#include <string.h>

extern "C" {
#include <caml/mlvalues.h>
#include <caml/callback.h>
#include <caml/memory.h>
#include <caml/alloc.h>
}

#include <string>

void init_ml() {
    static bool initialized = false;
    if (!initialized) {
        char *fake_argv[1] = {NULL};
        caml_startup(fake_argv);
        initialized = true;
    }
}

struct _MLValue {
    _MLValue() : val(Val_unit) {
        register_global_root(&val);
    }

    _MLValue(value v) : val(v) {
        register_global_root(&val);
    } 

    ~_MLValue() {
        remove_global_root(&val);
    }
    value val;
};

MLVal MLVal::find(const char *name) {
    init_ml();
    MLVal v;
    value *result = caml_named_value(name);
    if (!result) {
        printf("%s not found!\n", name);
        exit(1);
    }
    v.val.reset(new _MLValue(*result));
    return v;
}

MLVal MLValFromValue(value v) {
    if (Is_exception_result(v)) {
        printf("Can't make an MLVal from an exception!\n");
        exit(1);
    }
    MLVal mlv;
    mlv.val.reset(new _MLValue(v));
    return mlv;
}

value &MLVal::getValue() {
    return val->val;
}

MLVal MLVal::fromInt(int x) {
    return MLValFromValue(Val_int(x));
}

MLVal MLVal::fromFloat(float x) {
    return MLValFromValue(caml_copy_double(x));
}

MLVal MLVal::fromString(const char *str) {
    init_ml();
    value v = caml_alloc_string(strlen(str)+1);
    strcpy(String_val(v), str);
    return MLValFromValue(v);
}

MLVal MLVal::fromString(const std::string &str) {
    return MLVal::fromString(str.c_str());
}

MLVal MLVal::fromPointer(void *ptr) {
    return MLValFromValue((value)ptr);
}

MLVal MLVal::operator()() {
    return MLValFromValue(caml_callback(val->val, Val_unit));
}

MLVal MLVal::operator()(MLVal x) {
    return MLValFromValue(caml_callback(val->val, x.val->val));
}

MLVal MLVal::operator()(MLVal x, MLVal y) {
    return MLValFromValue(caml_callback2(val->val, x.val->val, y.val->val));
}


MLVal MLVal::operator()(MLVal x, MLVal y, MLVal z) {
    return MLValFromValue(caml_callback3(val->val, 
                                         x.val->val,
                                         y.val->val,
                                         z.val->val));
}


MLVal MLVal::operator()(MLVal x, MLVal y, MLVal z, MLVal w) {
    return (*this)(x, y, z)(w);
}

MLVal MLVal::operator()(MLVal a, MLVal b, MLVal c, MLVal d, MLVal e) {
    return (*this)(a, b, c)(d, e);
}
