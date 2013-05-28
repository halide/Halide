#ifndef HALIDE_EXTERN_H
#define HALIDE_EXTERN_H

/** \file
 * 
 * Convenience macros that lift functions that take C types into
 * functions that take and return exprs, and call the original
 * function at runtime under the hood. See test/c_function.cpp for
 * example usage.
 */

#define HalideExtern_1(rt, name, t1)                                    \
    Halide::Expr name(Halide::Expr a1) {                                \
        assert(a1.type() == Halide::type_of<t1>() && "Type mismatch for argument 1 of " #name); \
        return Halide::Internal::Call::make(Halide::type_of<rt>(), #name, vec(a1)); \
    }

#define HalideExtern_2(rt, name, t1, t2)                                \
    Halide::Expr name(Halide::Expr a1, Halide::Expr a2) {               \
        assert(a1.type() == Halide::type_of<t1>() && "Type mismatch for argument 1 of " #name); \
        assert(a2.type() == Halide::type_of<t2>() && "Type mismatch for argument 2 of " #name); \
        return Halide::Internal::Call::make(Halide::type_of<rt>(), #name, vec(a1, a2)); \
    }

#define HalideExtern_3(rt, name, t1, t2, t3)                            \
    Halide::Expr name(Halide::Expr a1, Halide::Expr a2, Halide::Expr a3) { \
        assert(a1.type() == Halide::type_of<t1>() && "Type mismatch for argument 1 of " #name); \
        assert(a2.type() == Halide::type_of<t2>() && "Type mismatch for argument 2 of " #name); \
        assert(a3.type() == Halide::type_of<t3>() && "Type mismatch for argument 3 of " #name); \
        return Halide::Internal::Call::make(Halide::type_of<rt>(), #name, vec(a1, a2, a3)); \
    }

#define HalideExtern_4(rt, name, t1, t2, t3, t4)                        \
    Halide::Expr name(Halide::Expr a1, Halide::Expr a2, Halide::Expr a3, Halide::Expr a4) { \
        assert(a1.type() == Halide::type_of<t1>() && "Type mismatch for argument 1 of " #name); \
        assert(a2.type() == Halide::type_of<t2>() && "Type mismatch for argument 2 of " #name); \
        assert(a3.type() == Halide::type_of<t3>() && "Type mismatch for argument 3 of " #name); \
        assert(a4.type() == Halide::type_of<t4>() && "Type mismatch for argument 4 of " #name); \
        return Halide::Internal::Call::make(Halide::type_of<rt>(), #name, vec(a1, a2, a3, a4)); \
  }

#endif
