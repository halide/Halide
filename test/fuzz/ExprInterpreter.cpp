#include "ExprInterpreter.h"
#include "Error.h"
#include "FindIntrinsics.h"
#include "IROperator.h"
#include "StrictifyFloat.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Halide {
namespace Internal {

ExprInterpreter::EvalValue::EvalValue(Type t) : type(t), lanes(t.lanes()) {
    for (int i = 0; i < t.lanes(); ++i) {
        if (t.is_float()) {
            lanes[i] = double{0.0};
        } else if (t.is_int()) {
            lanes[i] = int64_t{0};
        } else {
            lanes[i] = uint64_t{0};
        }
    }
}

template<typename F>
ExprInterpreter::EvalValue ExprInterpreter::apply_unary(Type t, const EvalValue &a, F f) {
    EvalValue res(t);
    for (int i = 0; i < t.lanes(); ++i) {
        res.lanes[i] = std::visit(
            [&f, &t](auto x) -> Scalar {
                auto out = f(x);
                if (t.is_float()) {
                    return static_cast<double>(out);
                }
                if (t.is_int()) {
                    return static_cast<int64_t>(out);
                }
                return static_cast<uint64_t>(out);
            },
            a.lanes[i]);
    }
    return res;
}

template<typename F>
ExprInterpreter::EvalValue ExprInterpreter::apply_binary(Type t, const EvalValue &a, const EvalValue &b, F f) {
    EvalValue res(t);
    for (int i = 0; i < t.lanes(); ++i) {
        res.lanes[i] = std::visit(
            [&f, &t](auto x, auto y) -> Scalar {
                static_assert(std::is_same_v<decltype(x), decltype(y)>);
                auto out = f(x, y);
                if (t.is_float()) {
                    return static_cast<double>(out);
                }
                if (t.is_int()) {
                    return static_cast<int64_t>(out);
                }
                return static_cast<uint64_t>(out);
            },
            a.lanes[i], b.lanes[i]);
    }
    return res;
}

template<typename F>
ExprInterpreter::EvalValue ExprInterpreter::apply_cmp(Type t, const EvalValue &a, const EvalValue &b, F f) {
    EvalValue res(t);
    for (int i = 0; i < t.lanes(); ++i) {
        res.lanes[i] = std::visit(
            [&f, &t](auto x, auto y) -> Scalar {
                static_assert(std::is_same_v<decltype(x), decltype(y)>);
                static_assert(std::is_same_v<decltype(f(x, y)), bool>);
                bool out = f(x, y);
                return static_cast<uint64_t>(out);
            },
            a.lanes[i], b.lanes[i]);
    }
    return res;
}

ExprInterpreter::EvalValue ExprInterpreter::eval(const Expr &e) {
    if (!e.defined()) {
        return EvalValue();
    }
    e.accept(this);
    truncate(result);
    return result;
}

void ExprInterpreter::truncate(EvalValue &v) {
    int b = v.type.bits();

    // Floats do not overflow/truncate in the same way,
    // and shifts >= 64 are Undefined Behavior in C++.
    if (v.type.is_float() || b >= 64) return;

    uint64_t mask = (1ULL << b) - 1;
    uint64_t sign_bit = 1ULL << (b - 1);

    for (int j = 0; j < v.type.lanes(); j++) {
        std::visit(
            [&](auto &x) {
                // Only apply truncation to integer variants (int64_t, uint64_t)
                if constexpr (std::is_integral_v<std::decay_t<decltype(x)>>) {
                    uint64_t u = static_cast<uint64_t>(x) & mask;

                    // If the underlying variant is signed, perform sign-extension
                    if constexpr (std::is_signed_v<std::decay_t<decltype(x)>>) {
                        if (u & sign_bit) {
                            u |= ~mask;
                        }
                    }

                    x = static_cast<std::decay_t<decltype(x)>>(u);
                }
            },
            v.lanes[j]);
    }
}

void ExprInterpreter::visit(const IntImm *op) {
    result = EvalValue(op->type);
    result.lanes[0] = (int64_t)op->value;
}

void ExprInterpreter::visit(const UIntImm *op) {
    result = EvalValue(op->type);
    result.lanes[0] = (uint64_t)op->value;
}

void ExprInterpreter::visit(const FloatImm *op) {
    result = EvalValue(op->type);
    result.lanes[0] = (double)op->value;
}

void ExprInterpreter::visit(const StringImm *op) {
    internal_error << "Cannot evaluate StringImm as a vector representation.";
}

void ExprInterpreter::visit(const Variable *op) {
    auto it = var_env.find(op->name);
    if (it != var_env.end()) {
        result = it->second;
    } else {
        internal_error << "Unbound variable in ExprInterpreter: " << op->name;
    }
}

void ExprInterpreter::visit(const Cast *op) {
    result = apply_unary(op->type, eval(op->value), [](auto x) { return x; });
}

void ExprInterpreter::visit(const Reinterpret *op) {
    EvalValue val = eval(op->value);
    result = EvalValue(op->type);

    int in_lanes = val.type.lanes();
    int in_bits = val.type.bits();
    int in_bytes = in_bits / 8;

    int out_lanes = op->type.lanes();
    int out_bits = op->type.bits();
    int out_bytes = out_bits / 8;

    int total_bytes = std::max(1, (in_bits * in_lanes) / 8);
    if (in_bytes == 0) {
        in_bytes = 1;
    }
    if (out_bytes == 0) {
        out_bytes = 1;
    }

    std::vector<char> buffer(total_bytes, 0);

    for (int j = 0; j < in_lanes; j++) {
        char *dst = buffer.data() + j * in_bytes;
        std::visit(
            [&](auto x) {
                if constexpr (std::is_floating_point_v<decltype(x)>) {
                    if (in_bits == 32) {
                        float f = static_cast<float>(x);
                        std::memcpy(dst, &f, 4);
                    } else if (in_bits == 64) {
                        std::memcpy(dst, &x, 8);
                    } else {
                        internal_error << "Unsupported float bit width in Reinterpret input";
                    }
                } else {
                    uint64_t u = static_cast<uint64_t>(x);
                    std::memcpy(dst, &u, in_bytes);
                }
            },
            val.lanes[j]);
    }

    for (int j = 0; j < out_lanes; j++) {
        const char *src = buffer.data() + j * out_bytes;
        if (op->type.is_float()) {
            if (out_bits == 32) {
                float f = 0.0f;
                std::memcpy(&f, src, 4);
                result.lanes[j] = static_cast<double>(f);
            } else if (out_bits == 64) {
                double f = 0.0;
                std::memcpy(&f, src, 8);
                result.lanes[j] = f;
            } else {
                internal_error << "Unsupported float bit width in Reinterpret output";
            }
        } else if (op->type.is_int()) {
            uint64_t u = 0;
            std::memcpy(&u, src, out_bytes);
            result.lanes[j] = static_cast<int64_t>(u);
        } else {
            uint64_t u = 0;
            std::memcpy(&u, src, out_bytes);
            result.lanes[j] = u;
        }
    }
}

void ExprInterpreter::visit(const Add *op) {
    result = apply_binary(op->type, eval(op->a), eval(op->b), [](auto x, auto y) { return x + y; });
}
void ExprInterpreter::visit(const Sub *op) {
    result = apply_binary(op->type, eval(op->a), eval(op->b), [](auto x, auto y) { return x - y; });
}
void ExprInterpreter::visit(const Mul *op) {
    result = apply_binary(op->type, eval(op->a), eval(op->b), [](auto x, auto y) { return x * y; });
}
void ExprInterpreter::visit(const Min *op) {
    result = apply_binary(op->type, eval(op->a), eval(op->b), [](auto x, auto y) { return std::min(x, y); });
}
void ExprInterpreter::visit(const Max *op) {
    result = apply_binary(op->type, eval(op->a), eval(op->b), [](auto x, auto y) { return std::max(x, y); });
}

void ExprInterpreter::visit(const EQ *op) {
    result = apply_cmp(op->type, eval(op->a), eval(op->b), [](auto x, auto y) { return x == y; });
}
void ExprInterpreter::visit(const NE *op) {
    result = apply_cmp(op->type, eval(op->a), eval(op->b), [](auto x, auto y) { return x != y; });
}
void ExprInterpreter::visit(const LT *op) {
    result = apply_cmp(op->type, eval(op->a), eval(op->b), [](auto x, auto y) { return x < y; });
}
void ExprInterpreter::visit(const LE *op) {
    result = apply_cmp(op->type, eval(op->a), eval(op->b), [](auto x, auto y) { return x <= y; });
}
void ExprInterpreter::visit(const GT *op) {
    result = apply_cmp(op->type, eval(op->a), eval(op->b), [](auto x, auto y) { return x > y; });
}
void ExprInterpreter::visit(const GE *op) {
    result = apply_cmp(op->type, eval(op->a), eval(op->b), [](auto x, auto y) { return x >= y; });
}

void ExprInterpreter::visit(const Div *op) {
    result = apply_binary(op->type, eval(op->a), eval(op->b), [](auto x, auto y) {
        if constexpr (std::is_floating_point_v<decltype(x)>) {
            return x / y;
        } else if constexpr (std::is_signed_v<decltype(x)>) {
            if (y == 0) {
                return decltype(x){0};
            }
            auto q = x / y;
            auto r = x % y;
            if (r != 0 && (r < 0) != (y < 0)) {
                q -= 1;
            }
            return q;
        } else {
            if (y == 0) {
                return decltype(x){0};
            }
            return x / y;
        }
    });
}

void ExprInterpreter::visit(const Mod *op) {
    result = apply_binary(op->type, eval(op->a), eval(op->b), [](auto x, auto y) {
        if constexpr (std::is_floating_point_v<decltype(x)>) {
            return std::fmod(x, y);
        } else if constexpr (std::is_signed_v<decltype(x)>) {
            if (y == 0) {
                return decltype(x){0};
            }
            auto r = x % y;
            if (r != 0 && (r < 0) != (y < 0)) {
                r += y;
            }
            return r;
        } else {
            if (y == 0) {
                return decltype(x){0};
            }
            return x % y;
        }
    });
}

void ExprInterpreter::visit(const And *op) {
    result = apply_binary(op->type, eval(op->a), eval(op->b), [](auto x, auto y) {
        if constexpr (std::is_integral_v<decltype(x)>) {
            return x & y;
        } else {
            internal_error << "Bitwise AND on floats";
            return x;
        }
    });
}

void ExprInterpreter::visit(const Or *op) {
    result = apply_binary(op->type, eval(op->a), eval(op->b), [](auto x, auto y) {
        if constexpr (std::is_integral_v<decltype(x)>) {
            return x | y;
        } else {
            internal_error << "Bitwise OR on floats";
            return x;
        }
    });
}

void ExprInterpreter::visit(const Not *op) {
    result = apply_unary(op->type, eval(op->a), [](auto x) {
        if constexpr (std::is_integral_v<decltype(x)>) {
            return ~x;
        } else {
            internal_error << "Bitwise NOT on floats";
            return x;
        }
    });
}

void ExprInterpreter::visit(const Select *op) {
    EvalValue cond = eval(op->condition), t = eval(op->true_value), f = eval(op->false_value);
    result = EvalValue(op->type);
    for (int j = 0; j < op->type.lanes(); j++) {
        bool c = std::visit([](auto x) { return x != 0; }, cond.lanes[j]);
        result.lanes[j] = c ? t.lanes[j] : f.lanes[j];
    }
}

void ExprInterpreter::visit(const Load *op) {
    internal_error << "Load nodes are unsupported without memory mapping in ExprInterpreter.";
}

void ExprInterpreter::visit(const Let *op) {
    EvalValue val = eval(op->value);
    auto old_val = var_env.find(op->name);
    bool had_old = (old_val != var_env.end());
    EvalValue old;
    if (had_old) {
        old = old_val->second;
    }

    var_env[op->name] = val;
    result = eval(op->body);

    if (had_old) {
        var_env[op->name] = old;
    } else {
        var_env.erase(op->name);
    }
}

void ExprInterpreter::visit(const Ramp *op) {
    EvalValue base = eval(op->base), stride = eval(op->stride);
    result = EvalValue(op->type);

    int n = base.type.lanes();  // The lane-width of the base and stride

    // ramp(b, s, l) = concat_vectors(b, b + s, b + 2*s, ... b + (l-1)*s)
    for (int j = 0; j < op->lanes; j++) {
        for (int k = 0; k < n; k++) {
            std::visit(
                [&](auto b, auto s) {
                    if constexpr (std::is_same_v<decltype(b), decltype(s)>) {
                        auto res = b + j * s;
                        if (op->type.is_float()) {
                            result.lanes[j * n + k] = static_cast<double>(res);
                        } else if (op->type.is_int()) {
                            result.lanes[j * n + k] = static_cast<int64_t>(res);
                        } else {
                            result.lanes[j * n + k] = static_cast<uint64_t>(res);
                        }
                    } else {
                        internal_error << "Ramp base and stride type mismatch";
                    }
                },
                base.lanes[k], stride.lanes[k]);
        }
    }
}

void ExprInterpreter::visit(const Broadcast *op) {
    EvalValue val = eval(op->value);
    result = EvalValue(op->type);
    int v_lanes = op->value.type().lanes();
    for (int j = 0; j < op->lanes; j++) {
        for (int k = 0; k < v_lanes; k++) {
            result.lanes[j * v_lanes + k] = val.lanes[k];
        }
    }
}

void ExprInterpreter::visit(const Shuffle *op) {
    std::vector<EvalValue> vecs;
    vecs.reserve(op->vectors.size());
    for (const Expr &e : op->vectors) {
        vecs.push_back(eval(e));
    }

    std::vector<Scalar> flat;
    for (const EvalValue &v : vecs) {
        for (int j = 0; j < v.type.lanes(); j++) {
            flat.push_back(v.lanes[j]);
        }
    }

    result = EvalValue(op->type);
    for (int j = 0; j < (int)op->indices.size(); j++) {
        int idx = op->indices[j];
        if (idx >= 0 && idx < (int)flat.size()) {
            result.lanes[j] = flat[idx];
        } else {
            internal_error << "Shuffle index out of bounds.";
        }
    }
}

void ExprInterpreter::visit(const VectorReduce *op) {
    EvalValue val = eval(op->value);
    result = EvalValue(op->type);
    int in_lanes = op->value.type().lanes();
    int out_lanes = op->type.lanes();
    int factor = in_lanes / out_lanes;

    for (int j = 0; j < out_lanes; j++) {
        Scalar res = val.lanes[j * factor];
        for (int k = 1; k < factor; k++) {
            Scalar next = val.lanes[j * factor + k];
            res = std::visit(
                [&](auto a, auto b) -> Scalar {
                    if constexpr (std::is_same_v<decltype(a), decltype(b)>) {
                        switch (op->op) {
                        case VectorReduce::Add:
                            return a + b;
                        case VectorReduce::Mul:
                            return a * b;
                        case VectorReduce::Min:
                            return std::min(a, b);
                        case VectorReduce::Max:
                            return std::max(a, b);
                        case VectorReduce::And:
                            if constexpr (std::is_integral_v<decltype(a)>) {
                                return a & b;
                            } else {
                                internal_error << "And on floats";
                                return a;
                            }
                        case VectorReduce::Or:
                            if constexpr (std::is_integral_v<decltype(a)>) {
                                return a | b;
                            } else {
                                internal_error << "Or on floats";
                                return a;
                            }
                        default:
                            internal_error << "Unhandled VectorReduce op";
                            return a;
                        }
                    } else {
                        internal_error << "VectorReduce type mismatch";
                        return a;
                    }
                },
                res, next);
        }

        std::visit(
            [&](auto x) {
                if (op->type.is_float()) {
                    result.lanes[j] = static_cast<double>(x);
                } else if (op->type.is_int()) {
                    result.lanes[j] = static_cast<int64_t>(x);
                } else {
                    result.lanes[j] = static_cast<uint64_t>(x);
                }
            },
            res);
    }
}

void ExprInterpreter::visit(const Call *op) {
    std::vector<EvalValue> args;
    args.reserve(op->args.size());
    for (const Expr &e : op->args) {
        args.push_back(eval(e));
    }
    result = EvalValue(op->type);

    if (op->is_intrinsic(Call::bitwise_and)) {
        result = apply_binary(op->type, args[0], args[1], [](auto a, auto b) {
            if constexpr (std::is_integral_v<decltype(a)>) {
                return a & b;
            } else {
                internal_error << "bitwise_and on float";
                return a;
            }
        });
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        result = apply_binary(op->type, args[0], args[1], [](auto a, auto b) {
            if constexpr (std::is_integral_v<decltype(a)>) {
                return a | b;
            } else {
                internal_error << "bitwise_or on float";
                return a;
            }
        });
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        result = apply_binary(op->type, args[0], args[1], [](auto a, auto b) {
            if constexpr (std::is_integral_v<decltype(a)>) {
                return a ^ b;
            } else {
                internal_error << "bitwise_xor on float";
                return a;
            }
        });
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        result = apply_unary(op->type, args[0], [](auto a) {
            if constexpr (std::is_integral_v<decltype(a)>) {
                return ~a;
            } else {
                internal_error << "bitwise_not on float";
                return a;
            }
        });
    } else if (op->is_intrinsic(Call::shift_left)) {
        result = apply_binary(op->type, args[0], args[1], [](auto a, auto b) {
            if constexpr (std::is_integral_v<decltype(a)>) {
                return a << b;
            } else {
                internal_error << "shift_left on float";
                return a;
            }
        });
    } else if (op->is_intrinsic(Call::shift_right)) {
        result = apply_binary(op->type, args[0], args[1], [](auto a, auto b) {
            if constexpr (std::is_integral_v<decltype(a)>) {
                return a >> b;
            } else {
                internal_error << "shift_right on float";
                return a;
            }
        });
    } else if (op->is_intrinsic(Call::abs)) {
        result = apply_unary(op->type, args[0], [](auto a) {
            if constexpr (std::is_floating_point_v<decltype(a)>) {
                return std::abs(a);
            } else if constexpr (std::is_signed_v<decltype(a)>) {
                return std::abs(a);
            } else {
                return a;
            }
        });
    } else if (op->is_intrinsic(Call::bool_to_mask) || op->is_intrinsic(Call::cast_mask)) {
        result = apply_unary(op->type, args[0], [](auto a) {
            if constexpr (std::is_integral_v<decltype(a)>) {
                return a ? static_cast<decltype(a)>(-1) : 0;
            } else {
                internal_error << "mask intrinsic on float";
                return int64_t{0};
            }
        });
    } else if (op->is_intrinsic(Call::select_mask) || op->is_intrinsic({Call::if_then_else, Call::if_then_else_mask})) {
        for (int j = 0; j < op->type.lanes(); j++) {
            bool cond = std::visit([](auto x) { return x != 0; }, args[0].lanes[j]);
            result.lanes[j] = cond ? args[1].lanes[j] : args[2].lanes[j];
        }
    } else if (op->is_intrinsic({Call::likely, Call::likely_if_innermost, Call::promise_clamped, Call::unsafe_promise_clamped})) {
        result = args[0];
    } else if (op->is_intrinsic({Call::return_second, Call::require})) {
        result = args[1];
    } else if (starts_with(op->name, "sin_")) {
        result = apply_unary(op->type, args[0], [](auto a) { return std::sin(a); });
    } else if (starts_with(op->name, "cos_")) {
        result = apply_unary(op->type, args[0], [](auto a) { return std::cos(a); });
    } else if (starts_with(op->name, "exp_")) {
        result = apply_unary(op->type, args[0], [](auto a) { return std::exp(a); });
    } else if (starts_with(op->name, "log_")) {
        result = apply_unary(op->type, args[0], [](auto a) { return std::log(a); });
    } else if (starts_with(op->name, "sqrt_")) {
        result = apply_unary(op->type, args[0], [](auto a) { return std::sqrt(a); });
    } else if (op->is_intrinsic(Call::strict_fma)) {
        internal_assert(op->args.size() == 3);
        internal_assert(op->args[0].type().is_float());
        for (int j = 0; j < op->type.lanes(); j++) {
            result.lanes[j] = std::visit(
                [&](auto a, auto b, auto c) -> Scalar {
                    if constexpr (std::is_same_v<decltype(a), decltype(b)> && std::is_same_v<decltype(b), decltype(c)>) {
                        auto out = std::fma(a, b, c);
                        if (op->type.is_float()) {
                            return static_cast<double>(out);
                        }
                        if (op->type.is_int()) {
                            return static_cast<int64_t>(out);
                        }
                        return static_cast<uint64_t>(out);
                    } else {
                        internal_error << "Type mismatch in strict_fma";
                        return double{0};
                    }
                },
                args[0].lanes[j], args[1].lanes[j], args[2].lanes[j]);
        }
    } else if (op->is_strict_float_intrinsic()) {
        Expr unstrict = unstrictify_float(op);
        unstrict.accept(this);
    } else if (op->is_arithmetic_intrinsic()) {
        Expr lower = lower_intrinsic(op);
        lower.accept(this);
    } else if (op->is_intrinsic(Call::absd)) {
        result = apply_binary(op->type, args[0], args[1], [](auto a, auto b) {
            return a < b ? b - a : a - b;
        });
    } else {
        internal_error << "Unhandled Call intrinsic / function in ExprInterpreter: " << op->name;
    }
}

namespace {

void test_scalar_equivalence() {
    ExprInterpreter interp;

    // 1. Integer scalar math equivalence
    auto math_test_int = [](const auto &x, const auto &y) {
        // Keeps values positive to align C++ truncation division with Halide's Euclidean division
        return (x + y) * (x - y) + (x / y) + (x % y);
    };

    int32_t cx = 42, cy = 5;
    int32_t c_res = math_test_int(cx, cy);

    Expr hx = Expr(cx), hy = Expr(cy);
    Expr h_ast = math_test_int(hx, hy);

    auto eval_res = interp.eval(h_ast);
    internal_assert(eval_res.type.is_int() && eval_res.type.bits() == 32 && eval_res.type.lanes() == 1);
    internal_assert(std::get<int64_t>(eval_res.lanes[0]) == c_res)
        << "Integer scalar evaluation mismatch. Expected: " << c_res
        << ", Got: " << std::get<int64_t>(eval_res.lanes[0]);

    // 2. Float scalar math equivalence
    using Halide::sin;
    using std::sin;
    auto math_test_float = [](const auto &x, const auto &y) {
        return (x * y) - sin(x / (y + 1.0f));
    };

    float fx = 3.14f, fy = 2.0f;
    float f_res = math_test_float(fx, fy);

    Expr hfx = Expr(fx), hfy = Expr(fy);
    Expr hf_ast = math_test_float(hfx, hfy);

    auto eval_f_res = interp.eval(hf_ast);
    internal_assert(eval_f_res.type.is_float() && eval_f_res.type.bits() == 32 && eval_f_res.type.lanes() == 1);

    double diff = std::abs(std::get<double>(eval_f_res.lanes[0]) - f_res);
    internal_assert(diff < 1e-5) << "Float scalar evaluation mismatch.";
}

void test_vector_operations() {
    ExprInterpreter interp;

    // 1. Ramp: create a vector <10, 13, 16, 19>
    Expr base = Expr(10);
    Expr stride = Expr(3);
    Expr ramp = Ramp::make(base, stride, 4);

    auto eval_ramp = interp.eval(ramp);
    internal_assert(eval_ramp.type.lanes() == 4);
    internal_assert(std::get<int64_t>(eval_ramp.lanes[0]) == 10);
    internal_assert(std::get<int64_t>(eval_ramp.lanes[1]) == 13);
    internal_assert(std::get<int64_t>(eval_ramp.lanes[2]) == 16);
    internal_assert(std::get<int64_t>(eval_ramp.lanes[3]) == 19);

    // 2. Broadcast: <5, 5, 5>
    Expr bc = Broadcast::make(Expr(5), 3);
    auto eval_bc = interp.eval(bc);
    internal_assert(eval_bc.type.lanes() == 3);
    internal_assert(std::get<int64_t>(eval_bc.lanes[0]) == 5);
    internal_assert(std::get<int64_t>(eval_bc.lanes[1]) == 5);
    internal_assert(std::get<int64_t>(eval_bc.lanes[2]) == 5);

    // 3. Shuffle: reverse the ramp -> <19, 16, 13, 10>
    Expr reversed = Shuffle::make({ramp}, {3, 2, 1, 0});
    auto eval_rev = interp.eval(reversed);
    internal_assert(eval_rev.type.lanes() == 4);
    internal_assert(std::get<int64_t>(eval_rev.lanes[0]) == 19);
    internal_assert(std::get<int64_t>(eval_rev.lanes[1]) == 16);
    internal_assert(std::get<int64_t>(eval_rev.lanes[2]) == 13);
    internal_assert(std::get<int64_t>(eval_rev.lanes[3]) == 10);

    // 4. VectorReduce: Sum the ramp -> 10 + 13 + 16 + 19 = 58
    Expr sum = VectorReduce::make(VectorReduce::Add, ramp, 1);
    auto eval_sum = interp.eval(sum);
    internal_assert(eval_sum.type.lanes() == 1);
    internal_assert(std::get<int64_t>(eval_sum.lanes[0]) == 58);

    // 5. Ramp of Ramp
    Expr ramp_of_ramp = Ramp::make(ramp, Broadcast::make(100, 4), 4);
    auto eval_ror = interp.eval(ramp_of_ramp);
    internal_assert(eval_ror.type.lanes() == 16);
    for (int i = 0; i < 4; ++i) {
        internal_assert(std::get<int64_t>(eval_ror.lanes[4 * i + 0]) == 100 * i + 10);
        internal_assert(std::get<int64_t>(eval_ror.lanes[4 * i + 1]) == 100 * i + 13);
        internal_assert(std::get<int64_t>(eval_ror.lanes[4 * i + 2]) == 100 * i + 16);
        internal_assert(std::get<int64_t>(eval_ror.lanes[4 * i + 3]) == 100 * i + 19);
    }

    // 6. Broadcast of Ramp
    Expr bc_of_ramp = Broadcast::make(ramp, 5);
    auto eval_bor = interp.eval(bc_of_ramp);
    internal_assert(eval_bor.type.lanes() == 20);
    for (int i = 0; i < 5; ++i) {
        internal_assert(std::get<int64_t>(eval_bor.lanes[4 * i + 0]) == 10);
        internal_assert(std::get<int64_t>(eval_bor.lanes[4 * i + 1]) == 13);
        internal_assert(std::get<int64_t>(eval_bor.lanes[4 * i + 2]) == 16);
        internal_assert(std::get<int64_t>(eval_bor.lanes[4 * i + 3]) == 19);
    }
}

void test_let_and_scoping() {
    ExprInterpreter interp;

    // Test: let x = 42 in (let x = x + 8 in x * 2)
    // Inner scoping should shadow outer scoping and evaluate cleanly
    Expr var_x = Variable::make(Int(32), "x");
    Expr inner_let = Let::make("x", var_x + Expr(8), var_x * Expr(2));
    Expr outer_let = Let::make("x", Expr(42), inner_let);

    auto res = interp.eval(outer_let);
    internal_assert(res.type.is_int() && res.type.lanes() == 1);

    // (42 + 8) * 2 = 100
    internal_assert(std::get<int64_t>(res.lanes[0]) == 100)
        << "Variable scoping / Let evaluation failed.";
}
}  // namespace

void ExprInterpreter::test() {
    test_scalar_equivalence();
    test_vector_operations();
    test_let_and_scoping();

    std::cout << "ExprInterpreter tests passed!" << "\n";
}

}  // namespace Internal
}  // namespace Halide
