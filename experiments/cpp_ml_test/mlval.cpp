#include "mlval.h"

#include <string.h>

extern "C" {
#include <caml/mlvalues.h>
#include <caml/callback.h>
#include <caml/memory.h>
#include <caml/alloc.h>
}



void init_ml() {
    char *fake_argv[1] = {NULL};
    caml_startup(fake_argv);
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
    static bool initialized = false;
    if (!initialized) {
        init_ml();
        initialized = true;
    }

    MLVal v;
    v.val.reset(new _MLValue(*caml_named_value(name)));
    return v;
}

MLVal MLValFromValue(value v) {
    MLVal mlv;
    mlv.val.reset(new _MLValue(v));
    return mlv;
}

MLVal::MLVal(int x) {
    val.reset(new _MLValue(Val_int(x)));
}

MLVal::MLVal(const char *str) {
    value v = caml_alloc_string(strlen(str)+1);
    strcpy(String_val(v), str);
    val.reset(new _MLValue(v));
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
