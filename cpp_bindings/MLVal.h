#ifndef MLVAL_H
#define MLVAL_H

#include <memory>

using std::shared_ptr;

// Opaque ocaml type
struct _MLValue;
class MLVal {
public:
    shared_ptr<_MLValue> val;
    static MLVal find(const char *name);
    MLVal() {};
    MLVal operator()();
    MLVal operator()(MLVal a);
    MLVal operator()(MLVal a, MLVal b);
    MLVal operator()(MLVal a, MLVal b, MLVal c);
    MLVal operator()(MLVal a, MLVal b, MLVal c, MLVal d);
    MLVal operator()(MLVal a, MLVal b, MLVal c, MLVal d, MLVal e);
    MLVal operator()(MLVal a, MLVal b, MLVal c, MLVal d, MLVal e, MLVal f);

    void *asVoidPtr();

    static MLVal fromString(const char *);
    static MLVal fromString(const std::string &);
    static MLVal fromInt(int);
    static MLVal fromFloat(float);
    static MLVal fromPointer(void *);

    static void unpackPair(MLVal input, MLVal &first, MLVal &second);
};



// Macro to help dig out functions
#define ML_FUNC0(n)                                            \
    MLVal n() {                                                \
        static MLVal callback;                                 \
        if (!callback.val) callback = MLVal::find(#n);         \
        return callback();                                     \
    }

#define ML_FUNC1(n)                                            \
    MLVal n(MLVal x) {                                         \
        static MLVal callback;                                 \
        if (!callback.val) callback = MLVal::find(#n);         \
        return callback(x);                                    \
    }

#define ML_FUNC2(n)                                            \
    MLVal n(MLVal x, MLVal y) {                                \
        static MLVal callback;                                 \
        if (!callback.val) callback = MLVal::find(#n);         \
        return callback(x, y);                                 \
    }

#define ML_FUNC3(n)                                            \
    MLVal n(MLVal x, MLVal y, MLVal z) {                       \
        static MLVal callback;                                 \
        if (!callback.val) callback = MLVal::find(#n);         \
        return callback(x, y, z);                              \
    }


#define ML_FUNC4(n)                                            \
  MLVal n(MLVal x, MLVal y, MLVal z, MLVal w) {                \
        static MLVal callback;                                 \
        if (!callback.val) callback = MLVal::find(#n);         \
        return callback(x, y, z, w);                           \
    }


#define ML_FUNC5(n)                                            \
    MLVal n(MLVal a, MLVal b, MLVal c, MLVal d, MLVal e) {     \
        static MLVal callback;                                 \
        if (!callback.val) callback = MLVal::find(#n);         \
        return callback(a, b, c, d, e);                        \
    }

#define ML_FUNC6(n)                                                    \
    MLVal n(MLVal a, MLVal b, MLVal c, MLVal d, MLVal e, MLVal f) {    \
        static MLVal callback;                                         \
        if (!callback.val) callback = MLVal::find(#n);                 \
        return callback(a, b, c, d, e, f);                             \
    }
 
#endif
