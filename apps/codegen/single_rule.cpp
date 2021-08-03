#include "Halide.h"
#include "rewrite_rule.h"
#include "single_rule.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace Halide;
using namespace Halide::Internal;

typedef std::map<std::string, std::string> VarScope;

std::string make_type_checker_condition(std::string var_name, std::string type_name, std::string output_name) {
    return "const " + type_name + " *" + output_name + " = " + var_name + ".as<" + type_name + ">()";
}

std::string make_new_unique_name() {
    static int counter = 0;
    return "t" + std::to_string(counter++);
}

size_t start_if_stmts(const Expr &expr, std::string current_name, VarScope &scope, std::ostream &stream, std::string indent);


template<typename BinOp>
inline size_t visit_bin_op(const BinOp *op, std::string opname, std::string current_name, VarScope &scope, std::ostream &stream, std::string indent) {
    std::string output_name = make_new_unique_name();
    std::string str_cond = make_type_checker_condition(current_name, opname, output_name);
    stream << indent << "if (" << str_cond << ") {\n";
    size_t i = start_if_stmts(op->a, output_name + "->a", scope, stream, indent + "\t");
    std::string indentation(i + 1, '\t');
    size_t j = start_if_stmts(op->b, output_name + "->b", scope, stream, indent + indentation);
    return i + j + 1;
}

size_t start_if_stmts(const Expr &expr, std::string current_name, VarScope &scope, std::ostream &stream, std::string indent = "\t") { // Probably will need some kind of environment for remembering names
    if (const Add *op = expr.as<Add>()) {
        return visit_bin_op<Add>(op, "Add", current_name, scope, stream, indent);
    } else if (const Sub *op = expr.as<Sub>()) {
        return visit_bin_op<Sub>(op, "Sub", current_name, scope, stream, indent);
    } else if (const Mul *op = expr.as<Mul>()) {
        return visit_bin_op<Mul>(op, "Mul", current_name, scope, stream, indent);
    } else if (const Div *op = expr.as<Div>()) {
        return visit_bin_op<Div>(op, "Div", current_name, scope, stream, indent);
    } else if (const Mod *op = expr.as<Mod>()) {
        return visit_bin_op<Mod>(op, "Mod", current_name, scope, stream, indent);
    } else if (const Min *op = expr.as<Min>()) {
        return visit_bin_op<Min>(op, "Min", current_name, scope, stream, indent);
    } else if (const Max *op = expr.as<Max>()) {
        return visit_bin_op<Max>(op, "Max", current_name, scope, stream, indent);
    } else if (const EQ *op = expr.as<EQ>()) {
        return visit_bin_op<EQ>(op, "EQ", current_name, scope, stream, indent);
    } else if (const NE *op = expr.as<NE>()) {
        return visit_bin_op<NE>(op, "NE", current_name, scope, stream, indent);
    } else if (const LT *op = expr.as<LT>()) {
        return visit_bin_op<LT>(op, "LT", current_name, scope, stream, indent);
    } else if (const LE *op = expr.as<LE>()) {
        return visit_bin_op<LE>(op, "LE", current_name, scope, stream, indent);
    } else if (const GT *op = expr.as<GT>()) {
        return visit_bin_op<GT>(op, "GT", current_name, scope, stream, indent);
    } else if (const GE *op = expr.as<GE>()) {
        return visit_bin_op<GE>(op, "GE", current_name, scope, stream, indent);
    } else if (const And *op = expr.as<And>()) {
        return visit_bin_op<And>(op, "And", current_name, scope, stream, indent);
    } else if (const Or *op = expr.as<Or>()) {
        return visit_bin_op<Or>(op, "Or", current_name, scope, stream, indent);
    } else if (const Not *op = expr.as<Not>()) {
        std::string output_name = make_new_unique_name();
        std::string str_cond = make_type_checker_condition(current_name, "Not", output_name);
        stream << indent << "if (" << str_cond << ") {\n";
        size_t i = start_if_stmts(op->a, output_name + "->a", scope, stream, indent + "\t");
        return i + 1;
    } else if (const Variable *op = expr.as<Variable>()) {
        // If the scope has this variable, we need to check equality. Otherwise, add it to the scope.
        auto iter = scope.find(op->name);
        if (iter != scope.end()) {
            stream << indent << "if (equal(" << current_name << ", " << iter->second << ") {\n";
            return 1;
        } else {
            scope[op->name] = current_name;
            return 0;
        }
    } else {
        // probably should throw an error
        assert(false);
        return 0;
    }
}

std::string build_expr(const Expr &expr, const VarScope &scope) {
    if (const Add *op = expr.as<Add>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " + " + b + ")";
    } else if (const Mul *op = expr.as<Mul>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " * " + b + ")";
    } else if (const Sub *op = expr.as<Sub>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " - " + b + ")";
    } else if (const Div *op = expr.as<Div>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " / " + b + ")";
    } else if (const Mod *op = expr.as<Mod>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " % " + b + ")";
    } else if (const Min *op = expr.as<Min>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "min(" + a + ", " + b + ")";
    } else if (const Max *op = expr.as<Max>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "max(" + a + ", " + b + ")";
    } else if (const EQ *op = expr.as<EQ>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " == " + b + ")";
    } else if (const NE *op = expr.as<NE>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " != " + b + ")";
    } else if (const LT *op = expr.as<LT>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " < " + b + ")";
    } else if (const LE *op = expr.as<LE>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " <= " + b + ")";
    } else if (const GT *op = expr.as<GT>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " > " + b + ")";
    } else if (const GE *op = expr.as<GE>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " >= " + b + ")";
    } else if (const And *op = expr.as<And>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " && " + b + ")";
    } else if (const Or *op = expr.as<Or>()) {
        std::string a = build_expr(op->a, scope);
        std::string b = build_expr(op->b, scope);
        return "(" + a + " || " + b + ")";
    } else if (const Not *op = expr.as<Not>()) {
        std::string a = build_expr(op->a, scope);
        return "(!" + a + ")";
    } else if (const Select *op = expr.as<Select>()) {
        std::string c = build_expr(op->condition, scope);
        std::string t = build_expr(op->true_value, scope);
        std::string f = build_expr(op->false_value, scope);
        return "select(" + c + ", " + t + ", " + f + ")";
    } else if (const Broadcast *op = expr.as<Broadcast>()) {
        std::string v = build_expr(op->value, scope);
        std::string l = build_expr(op->lanes, scope);
        return "broadcast(" + v + ", " + l + ")";
    } else if (const Ramp *op = expr.as<Ramp>()) {
        std::string b = build_expr(op->base, scope);
        std::string s = build_expr(op->stride, scope);
        std::string l = build_expr(op->lanes, scope);
        return "ramp(" + b + ", " + s + ", " + l + ")";
    } else if (const Variable *op = expr.as<Variable>()) {
        auto iter = scope.find(op->name);
        assert(iter != scope.end()); // TODO: if built inside Halide main code, use internal_assert or user_assert
        return iter->second;
    }  else if (const IntImm *op = expr.as<IntImm>()) {
        return std::to_string(op->value);
    } else {
        std::cerr << expr << "\n";
        assert(false);
        return "";
    }
    // TODO: add a bunch more cases
}

std::string build_predicate(const Expr &pred, const VarScope &scope) {
    return "evaluate_predicate(fold(" + build_expr(pred, scope) + "))";
}

void single_rule(const Expr &expr, const Expr &ret, std::string current_name, std::ostream &stream) {
    VarScope scope;
    size_t number_ifs = start_if_stmts(expr, current_name, scope, stream);
    std::string ret_indent(number_ifs + 1, '\t');
    stream << ret_indent << "return " << build_expr(ret, scope) << ";\n";
    for (size_t i = 0; i < number_ifs; i++) {
        std::string indentation(number_ifs - i, '\t');
        stream << indentation << "}\n";
    }
}

void single_rule_with_cond(const Expr &expr, const Expr &ret, const Expr &cond, std::string current_name, std::ostream &stream) {
    VarScope scope;
    size_t number_ifs = start_if_stmts(expr, current_name, scope, stream);
    // TODO: build condition checking if one exists
    std::string ret_indent(number_ifs + 1, '\t');
    stream << ret_indent << "if (" << build_predicate(cond, scope) << ") {\n";
    number_ifs++;
    stream << ret_indent << "\t" << "return " << build_expr(ret, scope) << ";\n";
    for (size_t i = 0; i < number_ifs; i++) {
        std::string indentation(number_ifs - i, '\t');
        stream << indentation << "}\n";
    }
}

std::string construct_simplifier_function(const std::vector<RewriteRule> &rules, std::string func_name, std::string var_name) {
    std::ostringstream stream;
    stream << "Expr " << func_name << "(const Expr &" << var_name << ") {\n";
    for (auto rule : rules) {
        if (rule.pred.defined()) {
            single_rule_with_cond(rule.before, rule.after, rule.pred, var_name, stream);
        } else {
            single_rule(rule.before, rule.after, var_name, stream);
        }
    }
    stream << "\treturn " << var_name << ";\n}\n";
    return stream.str();
}

/*
The below few functions should be added to any generated code.

std::string build_predicate(const Expr &pred, const VarScope &scope) {
    return "evaluate_predicate(fold(" + build_expr(pred, scope) + "))";
}
struct gsoc_scalar_value_t {
    union {
        bool b;
        int8_t i8;
        int16_t i16;
        int32_t i32;
        int64_t i64;
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        float f32;
        double f64;
        void *handle;
    } u;
#ifdef __cplusplus
    HALIDE_ALWAYS_INLINE gsoc_scalar_value_t() {
        u.u64 = 0;
    }
#endif
};
template<typename T>
T get(const gsoc_scalar_value_t &value) {
    if (std::is_same<T, int64_t>::value) {
        return value.u.i64;
    } else if (std::is_same<T, int32_t>::value) {
        return value.u.i32;
    }
}
gsoc_scalar_value_t fold(const Expr &expr) {
    gsoc_scalar_value_t ret;
    if (const IntImm *op = expr.as<IntImm>()) {
        ret.u.i64 = op->value;
    } else if (const Add *op = expr.as<Add>()) {
        if (op->type == Int(64)) {
            ret.u.i64 = get<int64_t>(fold(op->a)) + get<int64_t>(fold(op->b));
        } else if (op->type == Int(32)) {
            ret.u.i32 = get<int32_t>(fold(op->a)) + get<int32_t>(fold(op->b));
        }
    }
    // TODO: do the rest of the cases / types
    return ret;
}
bool evaluate_predicate(gsoc_scalar_value_t value) {
    return value.u.b;
}
*/
