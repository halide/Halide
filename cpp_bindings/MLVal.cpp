#include "MLVal.h"

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

struct MLVal::Contents {
    Contents() : val(Val_unit) {
        register_global_root(&val);
    }

    Contents(value v) : val(v) {
        register_global_root(&val);
    } 

    ~Contents() {
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
    v.contents.reset(new Contents(*result));
    return v;
}

MLVal MLValFromValue(value v) {
    if (Is_exception_result(v)) {
        printf("Can't make an MLVal from an exception!\n");
        exit(1);
    }
    return MLVal((void *)v);
}

void *MLVal::asVoidPtr() const {
    return (void *)(contents->val);
}

MLVal::MLVal(int x) : contents(new Contents(Val_int(x))) {
}

MLVal::MLVal(uint32_t x) : contents(new Contents(Val_int(x))) {
}

MLVal::MLVal(float x) {
    init_ml();
    contents.reset(new Contents(caml_copy_double((double)x)));
}

MLVal::MLVal(double x) {
    init_ml();
    contents.reset(new Contents(caml_copy_double(x)));
}

MLVal::MLVal(const char *str) {
    init_ml();
    value v = caml_alloc_string(strlen(str));
    strcpy(String_val(v), str);
    contents.reset(new Contents(v));
}

MLVal::MLVal(const std::string &str) {
    init_ml();
    value v = caml_alloc_string(str.size());
    strcpy(String_val(v), &str[0]);
    contents.reset(new Contents(v));    
}

MLVal::MLVal(void *ptr) : contents(new Contents((value)ptr)) {
}

void MLVal::unpackPair(const MLVal &tuple, MLVal &first, MLVal &second) {
    first = MLValFromValue(Field(tuple.contents->val, 0));
    second = MLValFromValue(Field(tuple.contents->val, 1));
}

MLVal MLVal::operator()() const {
    return MLValFromValue(caml_callback(contents->val, Val_unit));
}

MLVal MLVal::operator()(const MLVal &x) const {
    return MLValFromValue(caml_callback(contents->val, x.contents->val));
}

MLVal MLVal::operator()(const MLVal &x, const MLVal &y) const {
    return MLValFromValue(caml_callback2(contents->val, x.contents->val, y.contents->val));
}


MLVal MLVal::operator()(const MLVal &x, const MLVal &y, const MLVal &z) const {
    return MLValFromValue(caml_callback3(contents->val, 
                                         x.contents->val,
                                         y.contents->val,
                                         z.contents->val));
}


MLVal MLVal::operator()(const MLVal &x, const MLVal &y, const MLVal &z, const MLVal &w) const {
    return (*this)(x, y, z)(w);
}

MLVal MLVal::operator()(const MLVal &a, const MLVal &b, const MLVal &c, const MLVal &d, const MLVal &e) const {
    return (*this)(a, b, c)(d, e);
}

MLVal MLVal::operator()(const MLVal &a, const MLVal &b, const MLVal &c, const MLVal &d, const MLVal &e, const MLVal &f) const {
    return (*this)(a, b, c)(d, e, f);
}
