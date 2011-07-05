#include <stdio.h>

extern "C" {
#include <caml/mlvalues.h>
#include <caml/callback.h>
#include <caml/memory.h>
#include <caml/alloc.h>
}

#include <string.h>
#include <memory>

using std::shared_ptr;

class MLVal {
private:

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

    shared_ptr<_MLValue> val;

    MLVal(value v) {
        val.reset(new _MLValue(v));
    }

public:
    static MLVal find(const char *name) {
        return MLVal(*caml_named_value(name));
    }

    MLVal(int x) {
        val.reset(new _MLValue(Val_int(x)));
    }

    MLVal(const char *str) {
        value v = caml_alloc_string(strlen(str)+1);
        strcpy(String_val(v), str);
        val.reset(new _MLValue(v));
    }

    MLVal operator()() {
        return MLVal(caml_callback(val->val, Val_unit));
    }

    MLVal operator()(MLVal x) {
        return MLVal(caml_callback(val->val, x.val->val));
    }

    MLVal operator()(MLVal x, MLVal y) {
        return MLVal(caml_callback2(val->val, x.val->val, y.val->val));
    }


    MLVal operator()(MLVal x, MLVal y, MLVal z) {
        return MLVal(caml_callback3(val->val, 
                                    x.val->val,
                                    y.val->val,
                                    z.val->val));
    }

};

int main(int argc, char **argv) {
    caml_startup(argv);
    MLVal makeFoo1 = MLVal::find("makeFoo1");
    MLVal makeFoo2 = MLVal::find("makeFoo2");
    MLVal makeFoo3 = MLVal::find("makeFoo3");
    MLVal makeFoo4 = MLVal::find("makeFoo4");
    MLVal eatFoo = MLVal::find("eatFoo");
    
    eatFoo(makeFoo1());
    eatFoo(makeFoo2(1));
    eatFoo(makeFoo3("Hi!"));
    eatFoo(makeFoo4(17, 18));
    
    return 0;
}
