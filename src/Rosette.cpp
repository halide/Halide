#include "Rosette.h"

#include "Bounds.h"
#include "CSE.h"
#include "FindIntrinsics.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "IRVisitor.h"
#include "ModulusRemainder.h"
#include "Simplify.h"
#include "SExpParser.h"
#include "Substitute.h"
#include <fstream>
#include <stack>
#include <utility>
#include <set>

using namespace std;

namespace Halide {

namespace Internal {

namespace {

typedef std::map<std::string, VarEncoding> Encoding;

// Any variable that appears inside an indexing expression is encoded as an infinite integer.
// Everything else is encoded as bitvectors. If a variable is used in indexing and computation,
// current behavior is undefined
class InferVarEncodings : public IRVisitor {
    std::stack<bool> inside_indexing_expr;
    Encoding encoding;
    std::map<std::string, Expr> let_vars;
    std::map<std::string, Expr> llet_vars;

public:
    using IRVisitor::visit;

    InferVarEncodings(const std::map<std::string, Expr> &lvs, const std::map<std::string, Expr> &llvs) {
        inside_indexing_expr.push(false);
        let_vars = lvs;
        llet_vars = llvs;
    }

    void visit(const Variable *op) override {
        if (inside_indexing_expr.top()) {
            encoding[op->name] = Integer;
            
            // Recurse through let-expressions
            if (llet_vars.count(op->name)) {
                llet_vars[op->name].accept(this);
            }
        } else {
            encoding[op->name] = Bitvector;

            // Recurse through let-expressions
            if (llet_vars.count(op->name)) {
                let_vars[op->name].accept(this);
            }
        }
    }

    void visit(const Call *op) override {
        if (op->name == std::string("dynamic_shuffle")) {
            op->args[0].accept(this);
            inside_indexing_expr.push(true);
            for (unsigned int i = 1; i < op->args.size(); i++) {
                op->args[i].accept(this);
            }
            inside_indexing_expr.pop();
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Let *op) override {
        op->body.accept(this);
        if (encoding[op->name] == Integer) {
            inside_indexing_expr.push(true);
        }
        op->value.accept(this);
        if (encoding[op->name] == Integer) {
            inside_indexing_expr.pop();
        }
    }

    void visit(const Load *op) override {
        encoding[op->name] = (inside_indexing_expr.top() ? Integer : Bitvector);
        inside_indexing_expr.push(true);
        op->index.accept(this);
        op->predicate.accept(this);
        inside_indexing_expr.pop();
    }

    std::map<std::string, VarEncoding> getEncodings() {
        return encoding;
    }
};

// Takes the input Halide IR and converts it to Rosette syntax accepted by the Rake
// interpreter
class ExprPrinter : public VariadicVisitor<ExprPrinter, std::string, std::string> {

    IRPrinter printer;
    std::stack<int> indent;
    std::stack<VarEncoding> mode;

    // Context of the expression being printed:-
    //   - let_vars holds all of the let expressions defined in the program thus far
    //   - encoding maps variable names to the Rosette encoding that should be used for them
    std::map<std::string, Expr> let_vars;
    Encoding encoding;

    std::string tabs() {
        std::string ret = "";
        for (int i = 0; i < indent.top(); i++) {
            ret += " ";
        }
        return ret;
    }

    // This is for development and debug purposes, clumsily shuts down the compilation after
    // printing a NYI message.
    std::string NYI() {
        debug(0) << "\nNYI. \n";
        exit(0);
        return "";
    }

    // Helper functions

    std::string print_intrinsic (const std::string &name, std::vector<Expr> args, bool is_scalar_intrin) {
        if (is_scalar_intrin) {
            std::string rkt_args = "";

            indent.push(indent.top() + 1);
            for (unsigned int i = 0; i < args.size(); i++) {
                rkt_args += "\n" + dispatch(args[i]);
            }
            indent.pop();

            return tabs() + "(sca-" + name + rkt_args + ")";
        } else {
            std::string rkt_args = "";

            indent.push(indent.top() + 1);
            for (unsigned int i = 0; i < args.size(); i++) {
                rkt_args += "\n" + dispatch(args[i]);
            }
            indent.pop();

            return tabs() + "(vec-" + name + rkt_args + ")";
        }
    }

    std::string print_binary_op(const std::string& bv_name, const std::string& int_name, const Expr& a, const Expr &b, bool is_vector_op) {
        if (is_vector_op) {
            indent.push(indent.top() + 1);
            std::string rkt_lhs = dispatch(a);
            std::string rkt_rhs = dispatch(b);
            indent.pop();
            return tabs() + "(vec-" + bv_name + "\n" + rkt_lhs + "\n" + rkt_rhs + ")";
        } else {//if (mode.top() == VarEncoding::Bitvector) {
            indent.push(indent.top() + 1);
            std::string rkt_lhs = dispatch(a);
            std::string rkt_rhs = dispatch(b);
            indent.pop();
            return tabs() + "(sca-" + bv_name + " " + rkt_lhs + " " + rkt_rhs + ")";
        }
    }

public:

    // Constructor
    ExprPrinter(std::ostream &s, Encoding enc, std::map<std::string, Expr> lvs, int i = 1) : printer(s) {
        indent.push(i);
        mode.push(VarEncoding::Bitvector);
        let_vars = std::move(lvs);
        encoding = std::move(enc);
    }

    void int_mode() {
        while (!mode.empty()) {
            mode.pop();
        }
        mode.push(VarEncoding::Integer);
    }

    void bv_mode() {
        while (!mode.empty()) {
            mode.pop();
        }
        mode.push(VarEncoding::Bitvector);
    }

    /** Currently we do not do any re-writing at the Stmt level. Therefore, printing statements is not supported. */

    std::string visit(const VectorInstruction *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const LetStmt *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const AssertStmt *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const For *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const Provide *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const Store *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const Allocate *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const Evaluate *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const ProducerConsumer *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const Block *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const Realize *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const Prefetch *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const Free *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const Acquire *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const Fork *op) {
        printer.print(op);
        return NYI();
    }
    std::string visit(const IfThenElse *op) {
        printer.print(op);
        return NYI();
    }

    /* Constants and Variables */

    std::string visit(const Variable *op) {
        return tabs() + op->name;
    }

    std::string visit(const IntImm *op) {
        if (mode.top() == VarEncoding::Bitvector) {
            return tabs() + "(" + type_to_rake_type(op->type, false, true) + " (bv " +
                std::to_string(op->value) + " " + std::to_string(op->type.bits()) + "))";
        } else {
            return tabs() + std::to_string(op->value);
        }
    }

    std::string visit(const UIntImm *op) {
        if (mode.top() == VarEncoding::Bitvector) {
            return tabs() + "(" + type_to_rake_type(op->type, false, true) + " (bv " +
                std::to_string(op->value) + " " + std::to_string(op->type.bits()) + "))";
        } else {
            return tabs() + std::to_string(op->value);
        }
    }

    std::string visit(const FloatImm *op) {
        printer.print(op);
        return NYI();
    }

    std::string visit(const StringImm *op) {
        printer.print(op);
        return NYI();
    }

    /* Halide IR Operators */

    std::string visit(const Add *op) {
        return print_binary_op("add", "+", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const Sub *op) {
        return print_binary_op("sub", "-", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const Mul *op) {
        return print_binary_op("mul", "*", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const Div *op) {
        return print_binary_op("div", "quotient", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const Mod *op) {
        return print_binary_op("mod", "modulo", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const Min *op) {
        return print_binary_op("min", "min", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const Max *op) {
        return print_binary_op("max", "max", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const EQ *op) {
        return print_binary_op("eq", "eq?", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const NE *op) {
        return print_binary_op("ne", "ne?", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const LT *op) {
        return print_binary_op("lt", "<", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const LE *op) {
        return print_binary_op("le", "<=", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const GT *op) {
        return print_binary_op("gt", ">", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const GE *op) {
        return print_binary_op("ge", ">", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const And *op) {
        return print_binary_op("and", "and", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const Or *op) {
        return print_binary_op("or", "or", op->a, op->b, op->type.is_vector());
    }

    std::string visit(const Not *op) {
        if (op->type.is_vector()) {
            indent.push(indent.top() + 1);
            std::string rkt_val = dispatch(op->a);
            indent.pop();
            return tabs() + "(vec-not\n" + rkt_val + ")";
        } else if (mode.top() == VarEncoding::Bitvector) {
            return tabs() + "(sca-not " + dispatch(op->a) + ")";
        } else {
            return tabs() + "(not " + dispatch(op->a) + ")";
        }
    }

    std::string visit(const Broadcast *op) {
        indent.push(0);
        std::string rkt_type = std::to_string(op->lanes);
        std::string rkt_val = dispatch(op->value);
        indent.pop();
        return tabs() + "(x" + rkt_type + " " + rkt_val + ")";
    }

    std::string get_type_string(Type t) {
        std::ostringstream stream;
        stream << t;
        return stream.str();
    }

    std::string visit(const Cast *op) {
        const std::string type_string = get_type_string(op->type);

        if (op->type.is_scalar() && mode.top() == Integer) {
            return tabs() + dispatch(op->value);
        } 
        else if (op->type.is_scalar()) {
            const std::string rkt_val = dispatch(op->value);
            return tabs() + "(" + type_string + "x1 " + rkt_val + ")";
        } else {
            indent.push(indent.top() + 1);
            const std::string rkt_val = dispatch(op->value);
            indent.pop();
            return tabs() + "(" + type_string + "\n" + rkt_val + ")";
        }
    }

    std::string visit(const Let *op) {
        // Set the correct encoding mode
        mode.push(encoding[op->name]);
        std::string rkt_val = dispatch(op->value);
        mode.pop();

        indent.push(indent.top() + 1);
        std::string rkt_bdy = dispatch(op->body);
        indent.pop();

        return tabs() + "(let ([" + op->name + " " + rkt_val + "])\n" + rkt_bdy + ")";
    }

    std::string visit(const Call *op) {
        std::set<string> cpp_types{"int8_t", "int16_t", "int32_t", "int64_t",
                                   "uint8_t", "uint16_t", "uint32_t", "uint64_t"};
        std::set<string> arm_types{"arm:i8x8", "arm:u8x8", "arm:i16x4", "arm:u16x4", "arm:i32x2", "arm:u32x2", "arm:i64x1", "arm:u64x1",
                                   "arm:i8x16", "arm:u8x16", "arm:i16x8", "arm:u16x8", "arm:i32x4", "arm:u32x4", "arm:i64x2", "arm:u64x2",
                                   "arm:u16x16", "arm:i16x16", "arm:u32x8", "arm:i32x8", "arm:u64x4", "arm:i64x4", "arm:i8x32", "arm:u8x32",
                                   "arm:u8x4", "arm:i8x4"};
        std::set<string> x86_types{ // 256-bit types
                                   "x86:i8x32", "x86:u8x32",
                                   "x86:i16x16", "x86:u16x16",
                                   "x86:i32x8", "x86:u32x8",
                                   "x86:i64x4", "x86:u64x4",
                                   // 128-bit types
                                   "x86:i8x16", "x86:u8x16",
                                   "x86:i16x8", "x86:u16x8",
                                   "x86:i32x4", "x86:u32x4",
                                   "x86:i64x2", "x86:u64x2"
                                   };
        std::set<string> rake_fns{"halide:interpret"};

        if (cpp_types.count(op->name) || arm_types.count(op->name) || x86_types.count(op->name) || rake_fns.count(op->name)) {
            std::string rkt_args = "";

            indent.push(indent.top() + 1);
            for (unsigned int i = 0; i < op->args.size(); i++) {
                rkt_args += "\n" + dispatch(op->args[i]);
            }
            indent.pop();

            return tabs() + "(" + op->name + rkt_args + ")";
        }
        else if (op->is_intrinsic(Call::dynamic_shuffle)) {
            indent.push(indent.top() + 1);
            
            // Print the lookup table in (default) bit-vector mode
            std::string rkt_args = "\n" + dispatch(op->args[0]);
            
            // Print the lookup indexes in integer mode
            mode.push(VarEncoding::Integer);
            for (unsigned int i = 1; i < op->args.size(); i++) {
                rkt_args += "\n" + dispatch(op->args[i]);
            }
            mode.pop();
            indent.pop();

            return tabs() + "(" + op->name + rkt_args + ")";
        } 
        else if (op->is_intrinsic(Call::shift_right)) {
            return print_intrinsic("shr", op->args, op->type.is_scalar());
        }
        else if (op->is_intrinsic(Call::shift_left)) {
            return print_intrinsic("shl", op->args, op->type.is_scalar());
        } 
        else if (op->is_intrinsic(Call::absd)) {
            return print_intrinsic("absd", op->args, op->type.is_scalar());
        } 
        else if (op->is_intrinsic(Call::bitwise_and)) {
            return print_intrinsic("bwand", op->args, op->type.is_scalar());
        }
        else if (op->is_intrinsic(Call::bitwise_or)) {
            return print_intrinsic("bwor", op->args, op->type.is_scalar());
        }
        else if (op->is_intrinsic(Call::bitwise_not)) {
            return print_intrinsic("bwnot", op->args, op->type.is_scalar());
        }
        else if (op->is_intrinsic(Call::bitwise_xor)) {
            return print_intrinsic("bwxor", op->args, op->type.is_scalar());
        }
        else if (op->is_intrinsic(Call::count_leading_zeros)) {
            return print_intrinsic("clz", op->args, op->type.is_scalar());
        }
        else if (op->is_intrinsic(Call::if_then_else)) {
            vector<Expr> args_fixed = op->args;
            if (op->args[0].type().is_scalar()) {
                args_fixed[0] = Broadcast::make(op->args[0], op->args[1].type().lanes());
            }
            return print_intrinsic("if", args_fixed, op->type.is_scalar());
        }
        else {
            return print_intrinsic(op->name, op->args, op->type.is_scalar());
        }
    }

    std::string visit(const Reinterpret *op) {
        const std::string call_string = tabs() + "(vec-reinterpret" + "\n";
        const std::string type_string = get_type_string(op->type.element_of());
        indent.push(indent.top() + 1);
        const std::string arg = dispatch(op->value);
        const std::string full_type_string = "\n" + tabs() + "\'"  + type_string + " " + std::to_string(op->type.lanes());
        indent.pop();
        return call_string + arg + full_type_string + ")";
    }

    std::string visit(const Load *op) {
        indent.push(0);

        // Print index
        mode.push(VarEncoding::Integer);
        std::string rkt_idx = dispatch(op->index);
        std::string alignment = std::string("(aligned ") +
                                std::to_string(op->alignment.modulus) + std::string(" ") +
                                std::to_string(op->alignment.remainder) + ")";
        mode.pop();
        indent.pop();

        if (op->type.is_scalar() && mode.top() == VarEncoding::Integer) {
            return tabs() + "(" + op->name + " " + rkt_idx + ")";
        } else if (op->type.is_scalar()) {
            return tabs() + "(load-sca " + op->name + " " + rkt_idx + ")";
        } else {
            return tabs() + "(load " + op->name + " " + rkt_idx + " " + alignment + ")";
        }
    }

    std::string visit(const Ramp *op) {
        indent.push(0);
        std::string rkt_base = dispatch(op->base);
        std::string rkt_stride = dispatch(op->stride);
        std::string rkt_lanes = std::to_string(op->lanes);
        indent.pop();
        return tabs() + "(ramp " + rkt_base + " " + rkt_stride + " " + rkt_lanes + ")";
    }

    std::string visit(const Select *op) {
        if (op->type.is_vector()) {
            Expr cond = (op->condition.type().is_scalar() ?
                             Broadcast::make(op->condition, op->true_value.type().lanes()) :
                             op->condition);
            indent.push(indent.top() + 1);
            std::string rkt_cond = dispatch(cond);
            std::string rkt_true = dispatch(op->true_value);
            std::string rkt_false = dispatch(op->false_value);
            return tabs() + "(vec-if\n" + rkt_cond + "\n" + rkt_true + "\n" + rkt_false + ")";
        } else if (mode.top() == VarEncoding::Bitvector) {
            indent.push(indent.top() + 1);
            std::string rkt_cond = dispatch(op->condition);
            std::string rkt_true = dispatch(op->true_value);
            std::string rkt_false = dispatch(op->false_value);
            indent.pop();
            return tabs() + "(sca-if " + rkt_cond + " " + rkt_true + " " + rkt_false + ")";
        } else {
            std::string rkt_cond = dispatch(op->condition);
            std::string rkt_true = dispatch(op->true_value);
            std::string rkt_false = dispatch(op->false_value);
            return tabs() + "(if " + rkt_cond + " " + rkt_true + " " + rkt_false + ")";
        }
    }

    int log2(size_t value) {
        int log = 0;
        while (value >>= 1) {
            ++log;
        }
        return log;
    }

    std::string lower_concat(const Shuffle *op) {
        int indent_inc = log2(op->vectors.size());
        // + 1 if not perfect power of 2
        if (op->vectors.size() != (1ull << indent_inc)) {
            indent_inc++;
        }
        for (int i = 0; i < indent_inc; i++) {
            indent.push(indent.top() + 1);
        }
        std::vector<std::string> vectors(op->vectors.size());
        for (size_t i = 0; i < op->vectors.size(); i++) {
            vectors[i] = dispatch(op->vectors[i]);
        }

        int count = 0;
        int vector_length = op->vectors[0].type().lanes();
        // Combine pairwise concats
        while(vectors.size() > 1) {
            indent.pop();
            count++; // for sanity checking

            std::vector<std::string> new_vectors;

            for (size_t i = 0; i < (vectors.size() / 2); i++) {
                std::string sub_expr = tabs() + "(concat_vectors\n" + vectors[i] + "\n" + vectors[i + 1] + " " + std::to_string(vector_length) + ")";
                new_vectors.push_back(std::move(sub_expr));
            }

            // Tail case of an odd-length:
            if (vectors.size() % 2 == 1) {
                new_vectors.push_back(vectors.back());
            }

            vectors = std::move(new_vectors);
            vector_length *= 2;
        }
        internal_assert(count == indent_inc) << count << " vs " << indent_inc << " for Expr:\n\t" << Expr(op) << "\n";
        internal_assert(vectors.size() == 1)  << "expected single vector left in concat, instead got " << vectors.size() << "\n";
        return vectors[0];
    }

    std::string visit(const Shuffle *op) {
        if (op->is_slice()) {
            indent.push(indent.top() + 1);
            std::string rkt_vec = dispatch(op->vectors[0]);
            indent.pop();
            indent.push(0);
            mode.push(VarEncoding::Integer);
            std::string rkt_base = std::to_string(op->slice_begin());
            std::string rkt_stride = std::to_string(op->slice_stride());
            std::string rkt_len = std::to_string(op->indices.size());
            mode.pop();
            indent.pop();

            return tabs() + "(slice_vectors\n" + rkt_vec + " " + rkt_base + " " + rkt_stride + " " + rkt_len + ")";
        } 
        else if (op->is_broadcast()) {
            indent.push(indent.top() + 1);
            std::string rkt_vec = dispatch(op->vectors[0]);
            indent.pop();
            indent.push(0);
            mode.push(VarEncoding::Integer);
            std::string rkt_fac = std::to_string(op->broadcast_factor());
            mode.pop();
            indent.pop();

            return tabs() + "(vec-broadcast " + rkt_fac + "\n" + rkt_vec + ")";
        } 
        else if (op->is_interleave()) {
            switch (op->vectors.size()) {
                case 2: {
                    indent.push(indent.top() + 1);
                    std::string rkt_lhs = dispatch(op->vectors[0]);
                    std::string rkt_rhs = dispatch(op->vectors[1]);
                    indent.pop();
                    return tabs() + "(interleave\n" + rkt_lhs + "\n" + rkt_rhs + ")";
                }
                case 4: {
                    indent.push(indent.top() + 1);
                    indent.push(indent.top() + 1);
                    std::string rkt_vec0 = dispatch(op->vectors[0]);
                    std::string rkt_vec1 = dispatch(op->vectors[1]);
                    std::string rkt_vec2 = dispatch(op->vectors[2]);
                    std::string rkt_vec3 = dispatch(op->vectors[3]);
                    indent.pop();
                    std::string rkt_lhs = tabs() + "(interleave\n" + rkt_vec0 + "\n" + rkt_vec2 + ")";
                    std::string rkt_rhs = tabs() + "(interleave\n" + rkt_vec1 + "\n" + rkt_vec3 + ")";
                    indent.pop();
                    return tabs() + "(interleave\n" + rkt_lhs + "\n" + rkt_rhs + ")";
                }
                default:
                    return NYI();
            }
        } 
        else if (op->is_concat()) {
            return lower_concat(op);
        }
        printer.print(op);
        return NYI();
    }

    std::string visit(const VectorReduce *op) {
        std::string rkt_op = "";
        switch (op->op) {
        case VectorReduce::Add: rkt_op = "add"; break;
        case VectorReduce::SaturatingAdd: rkt_op = "sadd"; break;
        case VectorReduce::Mul: rkt_op = "mul"; break;
        case VectorReduce::Min: rkt_op = "min"; break;
        case VectorReduce::Max: rkt_op = "max"; break;
        case VectorReduce::And: rkt_op = "and"; break;
        case VectorReduce::Or: rkt_op = "or"; break;
        }
        indent.push(indent.top() + 1);
        std::string rkt_val = dispatch(op->value);
        indent.pop();
        return tabs() + "(vector_reduce '" + rkt_op + " " + 
            std::to_string(op->value.type().lanes() / op->type.lanes()) + "\n" + rkt_val + ")";
    }
};

void insert_encodings(Encoding &encoding, const Encoding &other) {
    for (const auto &item : other) {
        const auto [iter, success] = encoding.insert({item.first, item.second});
        if (!success) {
            internal_assert(iter->second == item.second) << "mismatching encodings: " << iter->first << " is: " << iter->second << " -> " << item.second << "\n";
        }
    }
}

class GatherVars : public IRVisitor {
  Scope<> let_vars;

public:
    using IRVisitor::visit;

    void visit(const Variable *var) override {
        if (!let_vars.contains(var->name)) {
            names.insert(var->name);
        }
    }

    void visit (const Let *let) override {
        let->value.accept(this);
        ScopedBinding<> binding(let_vars, let->name);
        let->body.accept(this);
    }

    std::set<std::string> names;
};

}  // anonymous namespace

Encoding get_encoding(const Expr &expr, const std::map<std::string, Expr> &let_vars, const std::map<std::string, Expr> &llet_vars) {
    // Infer which encoding to use for symbolic vars
    InferVarEncodings ive(let_vars, llet_vars);
    expr.accept(&ive);
    return ive.getEncodings();
}

std::string expr_to_racket(const Expr &expr, int indent) {
    std::map<std::string, Expr> let_vars; // Empty by default.
    const auto encoding = get_encoding(expr, let_vars, let_vars);
    return expr_to_racket(expr, encoding, let_vars, indent);
}

std::string expr_to_racket(const Expr &expr, const Encoding &encoding, const std::map<std::string, Expr> &let_vars, int indent) {
    // Print spec expr as Rosette code
    ExprPrinter specPrinter(std::cout, encoding, let_vars, indent);
    return specPrinter.dispatch(expr);
}

std::function<std::string(const Expr &, bool, bool)> get_expr_racket_dispatch(const Expr &expr, const Encoding &encoding, const std::map<std::string, Expr> &let_vars) {
    // Print spec expr as Rosette code
    // Reference: https://stackoverflow.com/questions/27775233/lambdas-and-capture-by-reference-local-variables-accessing-after-the-scope
    auto specPrinter = std::make_shared<ExprPrinter>(std::cout, encoding, let_vars);
    return [specPrinter](const Expr &expr, bool set_mode, bool int_mode) {
        // TODO: this got really messy... Is there a cleaner way?
        if (set_mode) {
            if (int_mode) {
                specPrinter->int_mode();
            } else {
                specPrinter->bv_mode();
            }
        }
        return specPrinter->dispatch(expr);
    };
}

std::string type_to_rake_type(Type type, bool include_space, bool c_plus_plus) {
    bool needs_space = true;
    std::ostringstream oss;

    if (type.is_bfloat()) {
        oss << "bfloat" << type.bits() << "_t";
    } else if (type.is_float()) {
        if (type.bits() == 32) {
            oss << "float";
        } else if (type.bits() == 64) {
            oss << "double";
        } else {
            oss << "float" << type.bits() << "_t";
        }
        if (type.is_vector()) {
            oss << type.lanes();
        }
    } else {
        switch (type.bits()) {
        case 1:
            // bool vectors are always emitted as uint8 in the C++ backend
            if (type.is_vector()) {
                oss << "uint8x" << type.lanes() << "_t";
            } else {
                oss << "uint1_t";
            }
            break;
        default:
            if (type.is_uint()) {
                oss << "u";
            }
            oss << "int" << type.bits();
            if (type.is_vector()) {
                oss << "x" << type.lanes();
            }
            oss << "_t";
        }
    }
    if (include_space && needs_space) {
        oss << " ";
    }
    return oss.str();
}

//>>>>>>>>>>>>>>>>> Rake Integration

namespace Rake {

bool should_inline_let(std::map<std::string, Expr> external_let_vars, const std::string& var_name) {
    if (external_let_vars.count(var_name)) {
        Expr e = external_let_vars[var_name];
        return e.node_type() == IRNodeType::Ramp || e.node_type() == IRNodeType::Load ||
               e.node_type() == IRNodeType::Broadcast;
    }
    return false;
}

// Extract the set of input variables that appear in the expression. These are modelled as symbolic
// constants in the synthesizer queries
class InferSymbolics : public IRVisitor {

    std::map<std::string, Expr> external_let_vars;
    std::map<std::string, Expr> external_llet_vars;
    Scope<Interval> &bounds;
    FuncValueBounds func_value_bounds;
    Encoding encoding;

    std::set<std::string> live_lets;
    std::set<const Variable *> live_vars;
    std::set<std::string> local_vars;
    std::set<std::pair<std::string, Type>> buffers;

public:
    using IRVisitor::visit;

    InferSymbolics(std::map<std::string, Expr> lvs, std::map<std::string, Expr> llvs, Scope<Interval> &bnds, 
        FuncValueBounds fvb, Encoding enc)
        : external_let_vars(std::move(lvs)), external_llet_vars(std::move(llvs)), bounds(bnds), func_value_bounds(std::move(fvb)), encoding(std::move(enc)) {
    }

    void visit(const Variable *op) override {
        if (op->type.is_vector()) {
            auto b = bounds_of_expr_in_scope(op, bounds, func_value_bounds);
            debug(1) << "Var Found: " << op->name << "\n"
                     << "Bounds: " << b.min << " ... " << b.max << "\n";
        }

        if (external_llet_vars.count(op->name) && encoding[op->name] == Integer) {
            external_llet_vars[op->name].accept(this);
            live_lets.insert(op->name);
        } else if (should_inline_let(external_let_vars, op->name)) {
            external_let_vars[op->name].accept(this);
            live_lets.insert(op->name);
        } else {
            live_vars.insert(op);
        }
    }

    void visit(const Let *op) override {
        local_vars.insert(op->name);
        IRVisitor::visit(op);
    }

    void visit(const Load *op) override {
        auto b = bounds_of_expr_in_scope(op, bounds, func_value_bounds);
        debug(1) << "Load Found: " << op->name << "\n"
                 << "Bound: " << b.min << " ... " << b.max << "\n";
        buffers.insert(std::pair<std::string, Type>(op->name, (op->type.is_vector() ? op->type.element_of() : op->type)));

        IRVisitor::visit(op);
    }

    std::set<const Variable *> getSymVars() {
        std::set<const Variable *> l;
        for (const auto *var : live_vars) {
            if (local_vars.count(var->name) == 0) {
                l.insert(var);
            }
        }
        return l;
    }

    std::set<std::pair<std::string, Type>> getSymBufs() {
        return buffers;
    }

    std::set<std::string> getLiveLets() {
        return live_lets;
    }
};

// This IR mutator optimizes vector expressions for the Hexagon HVX ISA
class IROptimizer : public IRMutator {
    std::set<const Variable *> symVars;
public:
    using IRMutator::mutate;

    enum Architecture {
        HVX, ARM, X86
    };

    IROptimizer(const FuncValueBounds &fvb, Architecture _arch, std::set<const BaseExprNode *> &ms, const std::map<std::string, Interval> &bounds)
        : arch(_arch), func_value_bounds(fvb), mutated_exprs(ms), variable_bounds(bounds) {
    }

    // We don't currently perform any optimizations at the Stmt level
    Stmt mutate(const Stmt &stmt) override {
        return IRMutator::mutate(stmt);
    }

    Expr mutate(const Expr &expr) override {
        if (arch == IROptimizer::HVX) {
            /* Disqualify expressions we do not currently support */

            // If the expression produces a scalar output, ignore it
            if (!expr.type().is_vector()) {
                return IRMutator::mutate(expr);
            }

            // If the expression produces an output of float type, ignore it
            if (expr.type().element_of().is_float()) {
                return IRMutator::mutate(expr);
            }

            // If the expression produces an output of boolean type, ignore it
            if (expr.type().element_of().is_bool()) {
                return IRMutator::mutate(expr);
            }

            // If the expression produces a vector that is not a multiple of the base vector length, ignore it
             if ((expr.type().bits() * expr.type().lanes() % 1024 != 0) && (expr.type().bits() > 1)) {
                 return IRMutator::mutate(expr);
             }

            // If the expression is a dynamic shuffle, ignore it
            const Call *c = expr.as<Call>();
            if (c && c->is_intrinsic(Call::dynamic_shuffle)) {
                return expr;
            }

            /* Ignore some qualifying but trivial expressions to reduce noise in the results */
            Expr base_e = expr;
            while (base_e.node_type() == IRNodeType::Let) {
                base_e = base_e.as<Let>()->body;
            }

            // If the expression is just a single ramp instruction, ignore it
            if (base_e.node_type() == IRNodeType::Ramp) {
                return IRMutator::mutate(expr);
            }

            // If the expression is just a single load instruction, ignore it
            if (base_e.node_type() == IRNodeType::Load) {
                return IRMutator::mutate(expr);
            }

            // If the expression is just a single broadcast instruction, ignore it
            if (base_e.node_type() == IRNodeType::Broadcast) {
                return IRMutator::mutate(expr);
            }

            // If the expression is just a variable, ignore it
            if (base_e.node_type() == IRNodeType::Variable) {
                return IRMutator::mutate(expr);
            }

            // If the expression is a conditional, optimize the branches individually
            if (base_e.node_type() == IRNodeType::Select) {
                return IRMutator::mutate(expr);
            }

            // Abstract out unsupported nodes if they appear as sub-expressions
            Expr spec_expr = AbstractUnsupportedNodes(IROptimizer::HVX, abstractions).mutate(expr);

            // Lower intrinsics
            spec_expr = LowerIntrinsics().mutate(spec_expr);

            // Lift cse for more readable specs
            spec_expr = common_subexpression_elimination(spec_expr);

            // Re-write expression using synthesis
            Expr optimized_expr = synthesize_impl(spec_expr, expr);

            // Replace abstracted abstractions
            Expr final_expr = ReplaceAbstractedNodes(abstractions, let_vars, symVars).mutate(optimized_expr);

            // Register that this node has been optimzied
            mutated_exprs.insert(final_expr.get());

            debug(0) << "\nOptimized expression: " << final_expr << "\n";
            
            return final_expr;
        }
        
        else if (arch == IROptimizer::ARM) {
            /* Disqualify expressions we do not currently support */
            
            // If the expression produces a scalar output, ignore it
            if (!expr.type().is_vector()) {
                return IRMutator::mutate(expr);
            }

            // If the expression produces an output of float type, ignore it
            if (expr.type().element_of().is_float()) {
                return IRMutator::mutate(expr);
            }

            // If the expression produces an output of boolean type, ignore it
            if (expr.type().element_of().is_bool()) {
                return IRMutator::mutate(expr);
            }

            // If the expression produces a vector that is not a multiple of the base vector length, ignore it
            if ((expr.type().bits() * expr.type().lanes() % 64 != 0) && (expr.type().bits() > 1)) {
                return IRMutator::mutate(expr);
            }

            // If the expression is a dynamic shuffle, ignore it
            const Call *c = expr.as<Call>();
            if (c && c->is_intrinsic(Call::dynamic_shuffle)) {
                return expr;
            }

            /* Ignore some qualifying but trivial expressions to reduce noise in the results */
            Expr base_e = expr;
            while (base_e.node_type() == IRNodeType::Let) {
                base_e = base_e.as<Let>()->body;
            }

            // If the expression is just a single ramp instruction, ignore it
            if (base_e.node_type() == IRNodeType::Ramp) {
                return IRMutator::mutate(expr);
            }

            // If the expression is just a single load instruction, ignore it
            if (base_e.node_type() == IRNodeType::Load) {
                return IRMutator::mutate(expr);
            }

            // If the expression is just a single broadcast instruction, ignore it
            if (base_e.node_type() == IRNodeType::Broadcast) {
                return IRMutator::mutate(expr);
            }

            // If the expression is just a variable, ignore it
            if (base_e.node_type() == IRNodeType::Variable) {
                return IRMutator::mutate(expr);
            }

            // If the expression is a conditional, optimize the branches individually
            if (base_e.node_type() == IRNodeType::Select) {
                return IRMutator::mutate(expr);
            }

            // Abstract out unsupported nodes if they appear as sub-expressions
            Expr spec_expr = AbstractUnsupportedNodes(IROptimizer::ARM, abstractions).mutate(expr);

            // Lower intrinsics
            spec_expr = LowerIntrinsics().mutate(spec_expr);

            // Lift cse for more readable specs
            spec_expr = common_subexpression_elimination(spec_expr);

            // Re-write expression using synthesis
            Expr optimized_expr = synthesize_impl(spec_expr, expr);

            // Replace abstracted abstractions
            Expr final_expr = ReplaceAbstractedNodes(abstractions, let_vars, symVars).mutate(optimized_expr);

            debug(0) << "\nOptimized expression: " << final_expr << "\n";

            return final_expr;
        }

        else if (arch == IROptimizer::X86) {
            /* Disqualify expressions we do not currently support */

            // If the expression produces a scalar output, ignore it
            if (!expr.type().is_vector()) {
                return IRMutator::mutate(expr);
            }

            // If the expression produces an output of float type, ignore it
            if (expr.type().element_of().is_float()) {
                return IRMutator::mutate(expr);
            }

            // If the expression produces an output of boolean type, ignore it
            if (expr.type().element_of().is_bool()) {
                return IRMutator::mutate(expr);
            }

            // If the expression produces a vector that is not a multiple of the base vector length, ignore it
            if ((expr.type().bits() * expr.type().lanes() % 128 != 0) && (expr.type().bits() > 1)) {
                return IRMutator::mutate(expr);
            }

            // If the expression is a dynamic shuffle, ignore it
            const Call *c = expr.as<Call>();
            if (c && c->is_intrinsic(Call::dynamic_shuffle)) {
                return expr;
            }

            /* Ignore some qualifying but trivial expressions to reduce noise in the results */
            Expr base_e = expr;
            while (base_e.node_type() == IRNodeType::Let) {
                base_e = base_e.as<Let>()->body;
            }

            // If the expression is just a single ramp instruction, ignore it
            if (base_e.node_type() == IRNodeType::Ramp) {
                return IRMutator::mutate(expr);
            }

            // If the expression is just a single load instruction, ignore it
            if (base_e.node_type() == IRNodeType::Load) {
                return IRMutator::mutate(expr);
            }

            // If the expression is just a single broadcast instruction, ignore it
            if (base_e.node_type() == IRNodeType::Broadcast) {
                return IRMutator::mutate(expr);
            }

            // If the expression is just a variable, ignore it
            if (base_e.node_type() == IRNodeType::Variable) {
                return IRMutator::mutate(expr);
            }

            // If the expression is a conditional, optimize the branches individually
            if (base_e.node_type() == IRNodeType::Select) {
                return IRMutator::mutate(expr);
            }

            // Abstract out unsupported nodes if they appear as sub-expressions
            Expr spec_expr = AbstractUnsupportedNodes(IROptimizer::X86, abstractions).mutate(expr);

            // Lower intrinsics
            spec_expr = LowerIntrinsics().mutate(spec_expr);

            // Lift cse for more readable specs
            spec_expr = common_subexpression_elimination(spec_expr);

            // Re-write expression using synthesis
            Expr optimized_expr = synthesize_impl(spec_expr, expr);

            // Replace abstracted abstractions
            Expr final_expr = ReplaceAbstractedNodes(abstractions, let_vars, symVars).mutate(optimized_expr);

            debug(0) << "\nOptimized expression: " << final_expr << "\n";

            return final_expr;
        }
        else {
            return expr;            
        }
    }

private:

    Architecture arch;
    const FuncValueBounds &func_value_bounds;
    std::set<const BaseExprNode *> &mutated_exprs;
    const std::map<std::string, Interval> &variable_bounds;
    Scope<Interval> bounds;
    
    std::map<std::string, Expr> let_vars;
    std::map<std::string, Expr> linearized_let_vars;
    std::vector<std::string> let_decl_order;

    std::map<std::string, Expr> abstractions;

    /* Helper functions and visitors */

    // A custom version of lower intrinsics that skips a TODO in the existing lower_intrinsics
    class LowerIntrinsics : public IRMutator {
        using IRMutator::visit;

        Expr widen(Expr a) {
            Type result_type = a.type().widen();
            return Cast::make(result_type, std::move(a));
        }

        Expr narrow(Expr a) {
            Type result_type = a.type().narrow();
            return Cast::make(result_type, std::move(a));
        }

        Expr visit(const Call *op) override {
            Expr lowered;
            // Generate cleaner specs. Since performance is not a concern, we can freely
            // use widening casts etc.
            if (op->is_intrinsic(Call::saturating_add)) {
                lowered = narrow(clamp(widen(op->args[0]) + widen(op->args[1]), 
                    op->args[0].type().min(), op->args[0].type().max()));
            }
            else if (op->is_intrinsic(Call::saturating_sub)) {
                lowered = narrow(clamp(widen(op->args[0]) - widen(op->args[1]),
                                       op->args[0].type().min(), op->args[0].type().max()));
            } 
            else if (op->is_intrinsic(Call::halving_add)) {
                lowered = narrow((widen(op->args[0]) + widen(op->args[1])) / 2);
            }
            else if (op->is_intrinsic(Call::halving_sub)) {
                lowered = narrow((widen(op->args[0]) - widen(op->args[1])) / 2);
            }
            else if (op->is_intrinsic(Call::rounding_halving_add)) {
                lowered = narrow((widen(op->args[0]) + widen(op->args[1]) + 1) / 2);
            } 
            // TODO: saturating_cast?
            else if (op->is_intrinsic(Call::sorted_avg)) {
                lowered = narrow((widen(op->args[0]) + widen(op->args[1])) / 2);
            } 
            else if (!op->is_intrinsic(Call::absd)) {
                    lowered = lower_intrinsic(op);
            }
            if (lowered.defined()) {
                return mutate(lowered);
            }
            return IRMutator::visit(op);
        }
    };

    class FloatFinder : public IRVisitor {
        using IRVisitor::visit;

        bool f = false;

        void visit(const Variable *op) override {
            if (op->type.is_float()) {
                f = true;
            }
        }

        void visit(const FloatImm *op) override {
            f = true;
        }

        void visit(const Cast *op) override {
            if (op->type.is_float()) {
                f = true;
            }
            return IRVisitor::visit(op);
        }

    public:
        bool found() {
            return f;
        }
    };

    class AbstractUnsupportedNodes : public IRMutator {
        using IRMutator::visit;

        std::map<std::string, Expr> &abstractions;
        Architecture _arch;
        
        bool abstract_ramps = true;

        Expr visit(const Call *op) override {
            if (op->is_intrinsic(Call::dynamic_shuffle)) {
                std::string uname = unique_name('t');
                abstractions[uname] = IRMutator::visit(op);
                return Variable::make(op->type, uname);
            }
            else if (op->is_intrinsic(Call::if_then_else)) {
                //debug(0) << "ITE found: " << op << "\n";
                std::string uname = unique_name('t');
                //debug(0) << "Replaced with: " << uname << "\n";
                abstractions[uname] = IRMutator::visit(op);
                return Variable::make(op->type, uname);
            }
            else {
                return IRMutator::visit(op);
            }
        }

        Expr visit(const Ramp *op) override {
            if (this->abstract_ramps) {
                std::string uname = unique_name('t');
                abstractions[uname] = IRMutator::visit(op);
                return Variable::make(op->type, uname);
            } 
            else {
                return IRMutator::visit(op);
            }
        }

        Expr visit(const Load *op) override {
            this->abstract_ramps = false;
            Expr r = IRMutator::visit(op);
            this->abstract_ramps = true;
            return r;
        }

        int get_vector_multiple() const {
            if (_arch == Architecture::ARM) {
                return 64;
            } else if (_arch == Architecture::X86) {
                return 128;
            } else {
                internal_assert(_arch == Architecture::HVX) << "can only support ARM | x86 | HVX architectures\n";
                return 1024;
            }
        }

        Expr visit(const Cast *op) override {
            Expr v = op->value;
            const int vec_len = get_vector_multiple();
            if (v.type().is_vector() && (v.type().bits() * v.type().lanes() % vec_len != 0) && (v.type().bits() > 1)) {
                std::string uname = unique_name('t');
                abstractions[uname] = IRMutator::visit(op);
                return Variable::make(op->type, uname); 
            }
            return IRMutator::visit(op);
        }

    public:

        AbstractUnsupportedNodes(Architecture a, std::map<std::string, Expr> &abstrs)
            : abstractions(abstrs), _arch(a) {
        }
    };

    class ReplaceAbstractedNodes : public IRMutator {
        using IRMutator::visit;

        std::map<std::string, Expr> &abstractions;
        std::map<std::string, Expr> &letvars;
        const std::set<const Variable *> &symVars;

        Expr visit(const Variable *v) override {
            if (abstractions.count(v->name) == 0) {
                return IRMutator::visit(v);
            }
            return abstractions[v->name];
        }

        Expr visit(const Load *v) override {
            // debug(0) << "LOAD " << Expr(v) << "\n";
            if (v->name.length() > 4) {
                // Trim the "-buf" suffix generated by rake
                std::string vname = v->name.substr(0, v->name.length() - 4);
                //debug(0) << vname << abstractions.count(vname) << "\n ";
                if (abstractions.count(vname) > 0) {
                    if (const Ramp *ramp = v->index.as<Ramp>()) {
                        return Shuffle::make_slice(
                            abstractions[vname], 
                            ((ramp->base).as<IntImm>())->value, 
                            ((ramp->stride).as<IntImm>())->value, 
                            ramp->lanes
                        );
                    } else {
                        return abstractions[vname];
                    }
                } else if (letvars.count(vname)) {
                    if (const Ramp *ramp = v->index.as<Ramp>()) {
                        return Shuffle::make_slice(
                            Variable::make(letvars[vname].type(), vname),
                            ((ramp->base).as<IntImm>())->value,
                            ((ramp->stride).as<IntImm>())->value,
                            ramp->lanes);
                    }
                } else if (v->name.substr(v->name.length() - 4, 4) == std::string("-buf")) {
                    if (const Ramp *ramp = v->index.as<Ramp>()) {
                        Type t = Int(0);
                        for (const auto & var : symVars) {
                            if (var->name == vname) {
                                t = var->type;
                            }
                        }
                        internal_assert(t != Int(0)) << "Could not find symVar for: " << v->name << "\n";

                        return Shuffle::make_slice(
                            // Todo: look for the actual variable, this type could be wrong
                            Variable::make(t, vname),
                            ((ramp->base).as<IntImm>())->value,
                            ((ramp->stride).as<IntImm>())->value,
                            ramp->lanes);
                    }
                }
            }
            return IRMutator::visit(v);
        }

    public:
        ReplaceAbstractedNodes(std::map<std::string, Expr> &abstrs, std::map<std::string, Expr> &lvs, const std::set<const Variable *> &_symVars)
            : abstractions(abstrs), letvars(lvs), symVars(_symVars) {}
    };

    bool containsFloat(const Expr &e) {
        FloatFinder ff;
        e.accept(&ff);
        return ff.found();
    }

    Expr linearize(const Expr &e) {
        if (is_const(e)) {
            return e;
        } else if (e.as<Variable>()) {
            return e;
        } else if (const Add *add = e.as<Add>()) {
            return linearize(add->a) + linearize(add->b);
        } else if (const Sub *sub = e.as<Sub>()) {
            return linearize(sub->a) - linearize(sub->b);
        } else if (const Mul *mul = e.as<Mul>()) {
            // Assume the simplifier has run, so constants are to the right
            if (is_const(mul->b)) {
                return linearize(mul->a) * mul->b;
            }
        } else if (const Min *m = e.as<Min>()) {
            return min(linearize(m->a), linearize(m->b));
        } else if (const Max *m = e.as<Max>()) {
            return max(linearize(m->a), linearize(m->b));
        }
        // TODO: Select nodes? Need to decide which kinds of
        // conditions are OK if so. Or we could abstract the condition
        // as a new variable.

        // Just treat it as a symbolic unknown
        std::string uname = unique_name('t');
        abstractions[uname] = e;
        return Variable::make(e.type(), uname);
    }

    /* Some visitor overrides to track the context within which each expr appears */
    using IRMutator::visit;

    Stmt visit(const LetStmt *stmt) override {
        // debug(0) << "Let Found: " << stmt->name << " = " << stmt->value << "\n";

        Expr value = stmt->value;
        value = LowerIntrinsics().mutate(value);
        value = AbstractUnsupportedNodes(arch, abstractions).mutate(value);

        bounds.push(stmt->name, bounds_of_expr_in_scope(value, bounds, func_value_bounds));
        let_vars[stmt->name] = value;
        linearized_let_vars[stmt->name] = linearize(value);
        let_decl_order.push_back(stmt->name);
        return IRMutator::visit(stmt);
    }

    // synthesis
    Expr synthesize_impl(Expr spec_expr, Expr orig_expr);
};

Expr IROptimizer::synthesize_impl(Expr spec_expr, Expr orig_expr) {
    static int expr_id = 0;

    debug(0) << "\nExpression ID: " << expr_id << "\n"
             << spec_expr << "\n\n";

    debug(0) << "Original expression: " << orig_expr << "\n";

    if (getenv("HALIDE_RAKE_DEBUG")) {
        int x;
        std::cin >> x;

        if (x == 0) {
            expr_id++;
            return orig_expr;
        }
    }

    Encoding encoding = get_encoding(spec_expr, let_vars, linearized_let_vars);

    // Infer symbolic variables
    InferSymbolics symFinder(let_vars, linearized_let_vars, bounds, func_value_bounds, encoding);
    spec_expr.accept(&symFinder);

    // Saved for parsing later.
    symVars = symFinder.getSymVars();

    auto spec_dispatch = get_expr_racket_dispatch(spec_expr, encoding, let_vars);
    std::string expr = spec_dispatch(spec_expr, false /* set_mode */, false /* int_mode */);

    // Prepare spec file for Rake
    std::stringstream axioms;
    std::stringstream sym_bufs;

    axioms << "(define axioms \n"
           << "  (list ";

    std::set<std::string> printed_vars;
    Encoding bounds_encodings;
    GatherVars bounds_vars;

    for (auto buf : symFinder.getSymBufs()) {
        debug(1) << "Symbolic buffer: " << buf.first << "\n";
        if (encoding[buf.first] == Integer) {
            sym_bufs << "(define-symbolic " << buf.first << " " << "(~> integer? integer?))\n";
        } else {
            sym_bufs << "(define-symbolic-buffer " << buf.first << " " << type_to_rake_type(buf.second, false, true) << ")\n";
        }

        printed_vars.insert(buf.first);

        std::pair<std::string, int> key(buf.first, 0);

        if (func_value_bounds.count(key)) {
            auto in = func_value_bounds.find(key)->second;
            if (!in.is_everything()) {
                if (!in.has_lower_bound()) {
                    in.min = in.max.type().min();
                }
                if (!in.has_upper_bound()) {
                    in.max = in.min.type().max();
                }
                if (!(containsFloat(in.min) || containsFloat(in.max))) {
                    debug(0) << "Bounds:\t" << buf.first << " : " << in.min << " ----- " << in.max << "\n";
                    axioms << "\n   (values-range-from "
                           << buf.first
                           << spec_dispatch(in.min, false /* set_mode */, false /* int_mode */)
                           << spec_dispatch(in.max, false /* set_mode */, false /* int_mode */) << ")";

                    std::map<std::string, Expr> let_vars;
                    auto temp_encoding = get_encoding(in.min, let_vars, linearized_let_vars);
                    insert_encodings(bounds_encodings, temp_encoding);
                    temp_encoding = get_encoding(in.max, let_vars, linearized_let_vars);
                    insert_encodings(bounds_encodings, temp_encoding);
                    // Gather all referred-to variables
                    in.min.accept(&bounds_vars);
                    in.max.accept(&bounds_vars);
                }
            }
        }
    }

    std::stringstream sym_vars;
    for (const auto *var : symFinder.getSymVars()) {
        debug(1) << "Symbolic var: " << var->name << " [" << encoding[var->name] << "]\n";

        if (var->type.is_vector() && !var->type.is_bool()) {
            sym_bufs << "(define-symbolic-buffer " << var->name << "-buf " << type_to_rake_type(var->type.element_of(), false, true) << ")\n";
            sym_vars << "(define " << var->name << " (load " << var->name
                     << "-buf (ramp 0 1 " << var->type.lanes() << ") (aligned 0 0)))\n";

            // debug(0) << "Finding bounds_of_expr_in_scope:\n";
            // debug(0) << "\t" << var->name << "\n";
            // debug(0) << "B{\n";
            // for (auto elem = bounds.cbegin(); elem != bounds.cend(); ++elem) {
            //     debug(0) << "\t" << elem.name() << " : [" << elem.value().min << ", " << elem.value().max << "]\n";
            // }
            // debug(0) << "}\nFVB{\n";
            // for (const auto &elem : func_value_bounds) {
            //     debug(0) << "\t{" << elem.first.first << ", " << elem.first.second << "} -> [" << elem.second.min << ", " << elem.second.max << "]\n";
            // }
            // debug(0) << "}\n";

            Interval in;
            const auto &iter = variable_bounds.find(var->name);
            if (iter != variable_bounds.end()) {
                in = iter->second;
            } else {
                in = bounds_of_expr_in_scope(var, bounds, func_value_bounds);
            }

            if (!in.is_everything()) {
                if (!in.has_lower_bound()) {
                    in.min = var->type.min();
                }
                if (!in.has_upper_bound()) {
                    in.max = var->type.max();
                }

                if (in.min.node_type() == IRNodeType::Broadcast) {
                    in.min = in.min.as<Broadcast>()->value;
                }

                if (in.max.node_type() == IRNodeType::Broadcast) {
                    in.max = in.max.as<Broadcast>()->value;
                }

                debug(0) << "Bounds:\t" << var->name << " : " << in.min << " ----- " << in.max << "\n";
                axioms << "\n   (values-range-from "
                       << var->name << "-buf"
                       << spec_dispatch(in.min, false /* set_mode */, false /* int_mode */)
                       << spec_dispatch(in.max, false /* set_mode */, false /* int_mode */) << ")";

                std::map<std::string, Expr> let_vars;
                auto temp_encoding = get_encoding(in.min, let_vars, linearized_let_vars);
                insert_encodings(bounds_encodings, temp_encoding);
                temp_encoding = get_encoding(in.max, let_vars, linearized_let_vars);
                insert_encodings(bounds_encodings, temp_encoding);
                // Gather all referred-to variables
                in.min.accept(&bounds_vars);
                in.max.accept(&bounds_vars);
            }
        } else {
            if (encoding[var->name] == Bitvector) {
                sym_vars << "(define-symbolic-var " << var->name
                         << " " << type_to_rake_type(var->type.element_of(), false, true) << ")\n";
            } else {
                sym_vars << "(define-symbolic " << var->name << " integer?)\n";
            }
        }
        printed_vars.insert(var->name);
    }

    axioms << "))\n";

    // Order let-stmts so we don't use any vars before they are defined
    std::set<std::string> live_lets = symFinder.getLiveLets();
    std::vector<std::string> ordered_live_lets(live_lets.begin(), live_lets.end());
    std::sort(
        ordered_live_lets.begin(),
        ordered_live_lets.end(),
        [this](const std::string &n1, const std::string& n2) -> int {
            int pos1 = std::find(let_decl_order.begin(), let_decl_order.end(), n1) - let_decl_order.begin();
            int pos2 = std::find(let_decl_order.begin(), let_decl_order.end(), n2) - let_decl_order.begin();
            return pos1 < pos2;
        });

    std::stringstream let_stmts;
    for (const auto& var_name : ordered_live_lets) {
        if (encoding[var_name] == Integer) {
            Expr val = linearized_let_vars[var_name];
            let_stmts << "(define " << var_name << " (var-lookup '" << var_name << spec_dispatch(val, true /* set_mode */, true /* int_mode */) << "))\n";
        } else {
            Expr val = let_vars[var_name];
            let_stmts << "(define " << var_name << spec_dispatch(val, true /* set_mode */, false /* int_mode */) << ")\n";
        }
        printed_vars.insert(var_name);
    }

    for (const auto &name : bounds_vars.names) {
        if (printed_vars.find(name) == printed_vars.end()) {
            internal_assert(bounds_encodings.count(name) != 0) << "Found bounds Variable with no encoding: " << name << "\n";
            internal_assert(bounds_encodings[name] == Bitvector) << "AJ didn't handle the bitvector case yet\n";
            sym_vars << "(define-symbolic " << name << " integer?)\n";
        }
    }

    const std::string benchmark_name = get_env_variable("HL_RAKE_BENCHMARK_NAME");
    internal_assert(!benchmark_name.empty()) << "Need to set HL_RAKE_BENCHMARK_NAME\n";
    const std::string filename = benchmark_name + "_expr_" + std::to_string(expr_id) + ".rkt";
    const std::string logging_filename = benchmark_name + "_expr_" + std::to_string(expr_id) + ".runtimes";
    const std::string output_filename = benchmark_name + "_sexp_" + std::to_string(expr_id) + ".out";

    std::ofstream rakeInputF;
    rakeInputF.open(filename.c_str());

    rakeInputF
        << "#lang rosette/safe\n"
        //<< "; " << stmt->value << "\n"
        << "\n"
        << "(require rake)\n"
        << "(init-logging \"" << logging_filename << "\")\n"
        << "\n"
        << sym_bufs.str()
        << sym_vars.str() << "\n"
        << axioms.str() << "\n"
        << let_stmts.str() << "\n"
        << "(define halide-expr\n"
        << expr << ")\n"
        << "\n"
        << "(define spec (synthesis-spec 'halide-ir halide-expr axioms))\n";
    
    if (arch == IROptimizer::HVX) {
        rakeInputF << "(define hvx-expr (synthesize-hvx spec 'greedy 'enumerative 'enumerative))\n"
                   << "\n"
                   << "(llvm-codegen hvx-expr \"" << output_filename << "\")";
    } else if (arch == IROptimizer::ARM) {
        rakeInputF << "(define arm-expr (synthesize-arm spec 'greedy 'enumerative 'enumerative))\n"
                   << "\n"
                   << "(arm:llvm-codegen arm-expr \"" << output_filename << "\")";
    } else if (arch == IROptimizer::X86) {
        rakeInputF << "(define x86-expr (synthesize-x86 spec 'greedy 'enumerative 'enumerative))\n"
                   << "\n"
                   << "(x86:llvm-codegen x86-expr \"" << output_filename << "\")";
    } else {
        internal_error << "Which architecture are you optimizing?\n";
    }

    rakeInputF.close();

    debug(0) << "Synthesis specification generated successfully: "
             << filename << "\n";

    if (getenv("HALIDE_RAKE_GENSPEC")) {
        expr_id++;
        return orig_expr;
    }

    std::ifstream cache(output_filename);
    if (!cache.good()) {
        char buf[1000];
        FILE *fp;
        std::string cmd = "racket " + filename;
        if ((fp = popen(cmd.c_str(), "r")) == nullptr) {
            printf("Error opening pipe!\n");
            exit(0);
        }

        while (fgets(buf, 100, fp) != nullptr) {
            debug(0) << buf;
        }

        if (pclose(fp)) {
            printf("Command not found or exited with error status\n");
            exit(0);
        }
    }

    std::ifstream in(output_filename);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    SExpParser p;
    Expr optimized = p.parse(s);

    expr_id++;

    return optimized;
}

class ARMCommuter : public IRMutator {
    using IRMutator::visit;

    bool should_commute(const Expr &a, const Expr &b) {
        if (a.node_type() < b.node_type()) {
            return true;
        }
        if (a.node_type() > b.node_type()) {
            return false;
        }

        if (a.node_type() == IRNodeType::Variable) {
            const Variable *va = a.as<Variable>();
            const Variable *vb = b.as<Variable>();
            return va->name.compare(vb->name) > 0;
        }

        // TODO: make this more powerful.

        return false;
    }

    Expr visit(const Call *op) override {
        static const std::set<std::string> commutable = {
            "rake.uaddl_u8x16",
            // TODO: add the rest.
        };

        if (commutable.count(op->name)) {
            internal_assert(op->args.size() == 2) << "Cannot commute: " << Expr(op) << "\n";
            Expr a = mutate(op->args[0]);
            Expr b = mutate(op->args[1]);
            if (should_commute(a, b)) {
                return Call::make(op->type, op->name, {b, a}, Call::CallType::PureExtern);
            } else {
                if (!a.same_as(op->args[0]) || !b.same_as(op->args[1])) {
                    return Call::make(op->type, op->name, {a, b}, Call::CallType::PureExtern);
                }
            }
        }
        return IRMutator::visit(op);
    }
};

} // namespace Rake

Stmt rake_optimize_hvx(const FuncValueBounds &fvb, const Stmt &s, std::set<const BaseExprNode *> &mutated_exprs,
                       const std::map<std::string, Interval> &bounds) {
    return Rake::IROptimizer(fvb, Rake::IROptimizer::HVX, mutated_exprs, bounds).mutate(s);
}

Expr rake_optimize_hvx(FuncValueBounds fvb, const Expr &e, std::set<const BaseExprNode *> &mutated_exprs,
                       const std::map<std::string, Interval> &bounds) {
    return Rake::IROptimizer(fvb, Rake::IROptimizer::HVX, mutated_exprs, bounds).mutate(e);
}

Stmt rake_optimize_arm(FuncValueBounds fvb, const Stmt &s, std::set<const BaseExprNode *> &mutated_exprs,
                       const std::map<std::string, Interval> &bounds) {
    return Rake::IROptimizer(fvb, Rake::IROptimizer::ARM, mutated_exprs, bounds).mutate(s);
}

Expr rake_optimize_arm(FuncValueBounds fvb, const Expr &e, std::set<const BaseExprNode *> &mutated_exprs,
                       const std::map<std::string, Interval> &bounds) {
    return Rake::IROptimizer(fvb, Rake::IROptimizer::ARM, mutated_exprs, bounds).mutate(e);
}

Stmt rake_optimize_x86(FuncValueBounds fvb, const Stmt &s, std::set<const BaseExprNode *> &mutated_exprs,
                       const std::map<std::string, Interval> &bounds) {
    return Rake::IROptimizer(fvb, Rake::IROptimizer::X86, mutated_exprs, bounds).mutate(s);
}

Stmt optimize_arm_instructions_synthesis(const Stmt &s, const Target &t, FuncValueBounds fvb) {
    // Print the IR before optimization
    // debug(0) << s << "\n\n";
    
    std::set<const BaseExprNode *> mutated_exprs;

    std::map<std::string, Interval> bounds;

    // Mutate IR expressions using Rake
    Stmt opt = rake_optimize_arm(std::move(fvb), s, mutated_exprs, bounds);
    
    // Do code cleanup: lift CSE, remove dead lets and simplify
    opt = simplify(opt);
    opt = common_subexpression_elimination(opt);
    opt = simplify(opt);

    // Print the IR after Rake's optimization
    // debug(1) << opt << "\n\n";

    // TODO: can we run arm legacy optimizations?
    return opt;
}

Stmt optimize_x86_instructions_synthesis(const Stmt &s, const Target &t, FuncValueBounds fvb) {
    // Print the IR before optimization
    // debug(0) << s << "\n\n";

    std::set<const BaseExprNode *> mutated_exprs;
    std::map<std::string, Interval> bounds;

    // Mutate IR expressions using Rake
    Stmt opt = rake_optimize_x86(std::move(fvb), s, mutated_exprs, bounds);

    // Do code cleanup: lift CSE, remove dead lets and simplify
    opt = simplify(opt);
    opt = common_subexpression_elimination(opt);
    opt = simplify(opt);

    // Print the IR after Rake's optimization
    // debug(1) << opt << "\n\n";

    // TODO: can we run x86 legacy optimizations?
    return opt;
}

// TODO: allow predicated assumptions. possibly via func value bounds?
Expr optimize_arm_instructions_synthesis(const Expr &expr, const Target &t, const std::map<std::string, Interval> &bounds) {
    // Print the IR before optimization
    // debug(0) << expr << "\n\n";
    std::set<const BaseExprNode *> mutated_exprs;

    FuncValueBounds fvb = empty_func_value_bounds();

    // Mutate IR expressions using Rake
    Expr opt = rake_optimize_arm(std::move(fvb), expr, mutated_exprs, bounds);

    // Do code cleanup: lift CSE, remove dead lets and simplify
    opt = simplify(opt);
    // TODO: should we do this after CSE?
    opt = simplify(Rake::ARMCommuter().mutate(opt));
    opt = common_subexpression_elimination(opt);
    opt = simplify(opt);

    // Print the IR after Rake's optimization
    // debug(1) << opt << "\n\n";

    // TODO: can we run arm legacy optimizations?
    return opt;
}

// TODO: allow predicated assumptions. possibly via func value bounds?
Expr optimize_hvx_instructions_synthesis(const Expr &expr, const Target &t, const std::map<std::string, Interval> &bounds) {
    // Print the IR before optimization
    // debug(0) << expr << "\n\n";
    std::set<const BaseExprNode *> mutated_exprs;

    FuncValueBounds fvb = empty_func_value_bounds();

    // Mutate IR expressions using Rake
    Expr opt = rake_optimize_hvx(std::move(fvb), expr, mutated_exprs, bounds);

    // Do code cleanup: lift CSE, remove dead lets and simplify
    opt = simplify(opt);
    // TODO: should we do this after CSE?
    // opt = simplify(Rake::ARMCommuter().mutate(opt));
    opt = common_subexpression_elimination(opt);
    opt = simplify(opt);

    // Print the IR after Rake's optimization
    // debug(1) << opt << "\n\n";

    // TODO: can we run arm legacy optimizations?
    return opt;
}

namespace {

class GetWidenedOrNarrowedVariables : public IRVisitor {
public:
    std::map<std::string, Expr> variables;
private:
    bool narrowing = false;
    bool widening = false;
protected:
    using IRVisitor::visit;

    void visit(const Variable *var) override {
        if (narrowing || widening) {
            variables[var->name] = var;
        }
    }

    void visit(const Cast *op) override {
        const bool was_narrowing = narrowing;
        const bool was_widening = widening;

        narrowing = (narrowing || (op->type.bits() < op->value.type().bits()));
        widening = (widening || (op->type.bits() > op->value.type().bits()));

        IRVisitor::visit(op);

        narrowing = was_narrowing;
        widening = was_widening;
    }

    void visit(const Call *op) override {
        const bool was_narrowing = narrowing;
        const bool was_widening = widening;

        // TODO: we could make a list, but this is easier.
        for (const auto &arg : op->args) {
            narrowing = (narrowing || (op->type.bits() < arg.type().bits()));
            widening = (widening || (op->type.bits() > arg.type().bits()));
        }

        IRVisitor::visit(op);

        narrowing = was_narrowing;
        widening = was_widening;
    }
};

std::map<std::string, Expr> get_widening_or_narrowing_variables(const Expr &expr) {
    GetWidenedOrNarrowedVariables getter;
    expr.accept(&getter);
    return getter.variables;
}

Interval make_reinterpret_predicate(const std::string &name, const Expr &expr) {
    const Type &t = expr.type().element_of();
    Expr min = t.min(), max = t.max();

    internal_assert(t.is_int_or_uint()) << expr;

    const bool is_signed = t.is_int();

    if (is_signed) {
        // Can we safely reinterpret as unsigned? must be positive.
        min = make_zero(t);
    } else {
        // Can we safely reinterpret as signed? must have MSB=0
        Type st = t.with_code(halide_type_int);
        max = cast(t, st.max());
    }

    internal_assert(min.type() == expr.type().element_of()) << min << " !t=" << expr << "\n";
    internal_assert(max.type() == expr.type().element_of()) << max << " !t= " << expr << "\n";

    return Interval(min, max);
}

std::set<std::set<std::string>> get_all_subsets_helper(const std::vector<std::string> &strs, const size_t i) {
    if (i == strs.size()) {
        return {};
    } else if (i == (strs.size() - 1)) {
        return {{}, {strs[i]}};
    } else {
        const auto subsets = get_all_subsets_helper(strs, i + 1);
        std::set<std::set<std::string>> ret;

        for (const auto &subset : subsets) {
            std::set<std::string> s = subset; // copy
            ret.insert(s);
            s.insert(strs[i]);
            ret.insert(s);
        }

        return ret;
    }
}

std::set<std::set<std::string>> get_all_subsets(const std::vector<std::string> &strs) {
    return get_all_subsets_helper(strs, 0);
}



std::pair<Expr, Expr> try_predicate_hvx(const Expr &expr, const std::set<std::string> &marked_vars, const std::map<std::string, Expr> &vars, const Target &t) {
    Expr predicate = const_true();
    std::map<std::string, Interval> bounds;

    for (const auto &var : marked_vars) {
        const auto iter = vars.find(var);
        internal_assert(iter != vars.end()) << "Expected variable: " << var << "\n";

        Interval in = make_reinterpret_predicate(var, iter->second);
        bounds[var] = in;
        predicate = predicate && ((iter->second <= in.max) && (iter->second >= in.min));
    }

    Expr opt = optimize_hvx_instructions_synthesis(expr, t, bounds);

    return {simplify(predicate), opt};
}

}

// Pair is <predicate, synthesized>
std::vector<std::pair<Expr, Expr>> try_predicates_hvx(const Expr &expr, const Target &t) {
    std::map<std::string, Expr> vars = get_widening_or_narrowing_variables(expr);

    std::vector<std::string> var_names;
    for (const auto &var : vars) {
        var_names.push_back(var.first);
    }

    auto subsets = get_all_subsets(var_names);

    std::vector<std::pair<Expr, Expr>> ret;

    for (const auto &subset : subsets) {
        auto p = try_predicate_hvx(expr, subset, vars, t);
        if (p.second.defined()) {
            ret.push_back(p);
        }
    }
    return ret;
}


}  // namespace Internal
}  // namespace Halide