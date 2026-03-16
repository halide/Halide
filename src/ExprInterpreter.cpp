#include "ExprInterpreter.h"
#include "Error.h"
#include "IROperator.h"

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
        res.lanes[i] = std::visit([&f, &t](auto x) -> Scalar {
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
        res.lanes[i] = std::visit([&f, &t](auto x, auto y) -> Scalar {
            if constexpr (std::is_same_v<decltype(x), decltype(y)>) {
                auto out = f(x, y);
                if (t.is_float()) {
                    return static_cast<double>(out);
                }
                if (t.is_int()) {
                    return static_cast<int64_t>(out);
                }
                return static_cast<uint64_t>(out);
            } else {
                internal_error << "Type mismatch in binary operation";
                return int64_t{0};
            }
        },
                                  a.lanes[i], b.lanes[i]);
    }
    return res;
}

template<typename F>
ExprInterpreter::EvalValue ExprInterpreter::apply_cmp(Type t, const EvalValue &a, const EvalValue &b, F f) {
    EvalValue res(t);
    for (int i = 0; i < t.lanes(); ++i) {
        res.lanes[i] = std::visit([&f, &t](auto x, auto y) -> Scalar {
            if constexpr (std::is_same_v<decltype(x), decltype(y)>) {
                uint64_t out = f(x, y) ? 1 : 0;
                if (t.is_float()) {
                    return static_cast<double>(out);
                }
                if (t.is_int()) {
                    return static_cast<int64_t>(out);
                }
                return static_cast<uint64_t>(out);
            } else {
                internal_error << "Type mismatch in comparison operation";
                return uint64_t{0};
            }
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
    if (!v.type.lanes()) {
        return;
    }
    int b = v.type.bits();
    if (b >= 64 || v.type.is_float()) {
        return;
    }

    if (v.type.is_int()) {
        int64_t m = (1ULL << b) - 1;
        int64_t sign_bit = 1ULL << (b - 1);
        for (int j = 0; j < v.type.lanes(); j++) {
            int64_t val = std::get<int64_t>(v.lanes[j]) & m;
            if (val & sign_bit) {
                val |= ~m;
            }
            v.lanes[j] = val;
        }
    } else {
        uint64_t m = (1ULL << b) - 1;
        for (int j = 0; j < v.type.lanes(); j++) {
            v.lanes[j] = std::get<uint64_t>(v.lanes[j]) & m;
        }
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
        std::visit([&](auto x) {
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
    std::visit([&](auto b, auto s) {
        if constexpr (std::is_same_v<decltype(b), decltype(s)>) {
            for (int j = 0; j < op->lanes; j++) {
                auto res = b + j * s;
                if (op->type.is_float()) {
                    result.lanes[j] = static_cast<double>(res);
                } else if (op->type.is_int()) {
                    result.lanes[j] = static_cast<int64_t>(res);
                } else {
                    result.lanes[j] = static_cast<uint64_t>(res);
                }
            }
        } else {
            internal_error << "Ramp base and stride type mismatch";
        }
    },
               base.lanes[0], stride.lanes[0]);
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
            res = std::visit([&](auto a, auto b) -> Scalar {
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

        std::visit([&](auto x) {
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
    } else if (op->name == "sin") {
        result = apply_unary(op->type, args[0], [](auto a) { return std::sin(a); });
    } else if (op->name == "cos") {
        result = apply_unary(op->type, args[0], [](auto a) { return std::cos(a); });
    } else if (op->name == "exp") {
        result = apply_unary(op->type, args[0], [](auto a) { return std::exp(a); });
    } else if (op->name == "log") {
        result = apply_unary(op->type, args[0], [](auto a) { return std::log(a); });
    } else if (op->name == "sqrt") {
        result = apply_unary(op->type, args[0], [](auto a) { return std::sqrt(a); });
    } else if (op->is_intrinsic(Call::strict_add)) {
        result = apply_binary(op->type, args[0], args[1], [](auto a, auto b) { return a + b; });
    } else if (op->is_intrinsic(Call::strict_sub)) {
        result = apply_binary(op->type, args[0], args[1], [](auto a, auto b) { return a - b; });
    } else if (op->is_intrinsic(Call::strict_mul)) {
        result = apply_binary(op->type, args[0], args[1], [](auto a, auto b) { return a * b; });
    } else if (op->is_intrinsic(Call::strict_div)) {
        result = apply_binary(op->type, args[0], args[1], [](auto a, auto b) { return a / b; });
    } else if (op->is_intrinsic(Call::strict_fma)) {
        for (int j = 0; j < op->type.lanes(); j++) {
            result.lanes[j] = std::visit([&](auto a, auto b, auto c) -> Scalar {
                if constexpr (std::is_same_v<decltype(a), decltype(b)> && std::is_same_v<decltype(b), decltype(c)>) {
                    auto out = a * b + c;
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
    } else if (op->is_intrinsic(Call::absd)) {
        result = apply_binary(op->type, args[0], args[1], [](auto a, auto b) {
            return a < b ? b - a : a - b;
        });
    } else {
        internal_error << "Unhandled Call intrinsic / function in ExprInterpreter: " << op->name;
    }
}

}  // namespace Internal
}  // namespace Halide
