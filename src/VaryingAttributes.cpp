#include "VaryingAttributes.h"

#include <algorithm>

#include "CodeGen_GPU_Dev.h"

#include "CSE.h"
#include "IRMutator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

Stmt make_block(Stmt first, Stmt rest) {
    if (first.defined() && rest.defined()) {
        return Block::make(first, rest);
    } else if (first.defined()) {
        return first;
    } else {
        return rest;
    }
}

// Find expressions that we can evaluate with interpolation hardware in the GPU
//
// This visitor keeps track of the "order" of the expression in terms of the
// specified variables. The order value 0 means that the expression is contant;
// order value 1 means that it is linear in terms of only one variable, check
// the member found to determine which; order value 2 means non-linear, it
// could be disqualified due to being quadratic, bilinear or the result of an
// unknown function.
class FindLinearExpressions : public IRMutator {
protected:
    using IRMutator::visit;

    bool in_glsl_loops;

    Expr tag_linear_expression(Expr e, const std::string &name = unique_name('a')) {

        internal_assert(name.length() > 0);

        if (total_found >= max_expressions) {
            return e;
        }

        // Wrap the expression with an intrinsic to tag that it is a varying
        // attribute. These tagged variables will be pulled out of the fragment
        // shader during a subsequent pass
        Expr intrinsic = Call::make(e.type(), Call::glsl_varying,
                                    {name + ".varying", e},
                                    Call::Intrinsic);
        ++total_found;

        return intrinsic;
    }

    Expr visit(const Call *op) override {
        std::vector<Expr> new_args = op->args;

        // Check to see if this call is a load
        if (op->is_intrinsic(Call::glsl_texture_load)) {
            // Check if the texture coordinate arguments are linear wrt the GPU
            // loop variables
            internal_assert(!loop_vars.empty()) << "No GPU loop variables found at texture load\n";

            // Iterate over the texture coordinate arguments
            for (int i = 2; i != 4; ++i) {
                new_args[i] = mutate(op->args[i]);
                if (order == 1) {
                    new_args[i] = tag_linear_expression(new_args[i]);
                }
            }
        } else if (op->is_intrinsic(Call::glsl_texture_store)) {
            // Check if the value expression is linear wrt the loop variables
            internal_assert(!loop_vars.empty()) << "No GPU loop variables found at texture store\n";

            // The value is the 5th argument to the intrinsic
            new_args[5] = mutate(new_args[5]);
            if (order == 1) {
                new_args[5] = tag_linear_expression(new_args[5]);
            }
        }

        // The texture lookup itself is counted as a non-linear operation
        order = 2;
        return Call::make(op->type, op->name, new_args, op->call_type,
                          op->func, op->value_index, op->image, op->param);
    }

    Expr visit(const Let *op) override {
        Expr mutated_value = mutate(op->value);
        int value_order = order;

        ScopedBinding<int> bind(scope, op->name, order);

        Expr mutated_body = mutate(op->body);

        if ((value_order == 1) && (total_found < max_expressions)) {
            // Wrap the let value with a varying tag
            mutated_value = Call::make(mutated_value.type(), Call::glsl_varying,
                                       {op->name + ".varying", mutated_value},
                                       Call::Intrinsic);
            ++total_found;
        }

        return Let::make(op->name, mutated_value, mutated_body);
    }

    Stmt visit(const For *op) override {
        bool old_in_glsl_loops = in_glsl_loops;
        bool kernel_loop = op->device_api == DeviceAPI::GLSL;
        bool within_kernel_loop = !kernel_loop && in_glsl_loops;
        // Check if the loop variable is a GPU variable thread variable and for GLSL
        if (kernel_loop) {
            loop_vars.push_back(op->name);
            in_glsl_loops = true;
        } else if (within_kernel_loop) {
            // The inner loop variable is non-linear w.r.t the glsl pixel coordinate.
            scope.push(op->name, 2);
        }

        Stmt mutated_body = mutate(op->body);

        if (kernel_loop) {
            loop_vars.pop_back();
        } else if (within_kernel_loop) {
            scope.pop(op->name);
        }

        in_glsl_loops = old_in_glsl_loops;

        if (mutated_body.same_as(op->body)) {
            return op;
        } else {
            return For::make(op->name, op->min, op->extent, op->for_type, op->device_api, mutated_body);
        }
    }

    Expr visit(const Variable *op) override {
        if (std::find(loop_vars.begin(), loop_vars.end(), op->name) != loop_vars.end()) {
            order = 1;
        } else if (scope.contains(op->name)) {
            order = scope.get(op->name);
        } else {
            // If the variable is not found in scope, then we assume it is
            // constant in terms of the independent variables.
            order = 0;
        }
        return op;
    }

    Expr visit(const IntImm *op) override {
        order = 0;
        return op;
    }
    Expr visit(const UIntImm *op) override {
        order = 0;
        return op;
    }
    Expr visit(const FloatImm *op) override {
        order = 0;
        return op;
    }
    Expr visit(const StringImm *op) override {
        order = 0;
        return op;
    }

    Expr visit(const Cast *op) override {

        Expr mutated_value = mutate(op->value);
        int value_order = order;

        // We can only interpolate float values, disqualify the expression if
        // this is a cast to a different type
        if (order && (!op->type.is_float())) {
            order = 2;
        }

        if ((order > 1) && (value_order == 1)) {
            mutated_value = tag_linear_expression(mutated_value);
        }

        return Cast::make(op->type, mutated_value);
    }

    // Add and subtract do not make the expression non-linear, if it is already
    // linear or constant
    template<typename T>
    Expr visit_binary_linear(T *op) {
        Expr a = mutate(op->a);
        unsigned int order_a = order;
        Expr b = mutate(op->b);
        unsigned int order_b = order;

        order = std::max(order_a, order_b);

        // If the whole expression is greater than linear, check to see if
        // either argument is linear and if so, add it to a candidate list
        if ((order > 1) && (order_a == 1)) {
            a = tag_linear_expression(a);
        }
        if ((order > 1) && (order_b == 1)) {
            b = tag_linear_expression(b);
        }

        return T::make(a, b);
    }

    Expr visit(const Add *op) override {
        return visit_binary_linear(op);
    }
    Expr visit(const Sub *op) override {
        return visit_binary_linear(op);
    }

    // Multiplying increases the order of the expression, possibly making it
    // non-linear
    Expr visit(const Mul *op) override {
        Expr a = mutate(op->a);
        unsigned int order_a = order;
        Expr b = mutate(op->b);
        unsigned int order_b = order;

        order = order_a + order_b;

        // If the whole expression is greater than linear, check to see if
        // either argument is linear and if so, add it to a candidate list
        if ((order > 1) && (order_a == 1)) {
            a = tag_linear_expression(a);
        }
        if ((order > 1) && (order_b == 1)) {
            b = tag_linear_expression(b);
        }

        return Mul::make(a, b);
    }

    // Dividing is either multiplying by a constant, or makes the result
    // non-linear (i.e. order -1)
    Expr visit(const Div *op) override {
        Expr a = mutate(op->a);
        unsigned int order_a = order;
        Expr b = mutate(op->b);
        unsigned int order_b = order;

        if (order_a && !order_b) {
            // Case: x / c
            order = order_a;
        } else if (!order_a && order_b) {
            // Case: c / x
            order = 2;
        } else {
            order = order_a + order_b;
        }

        if ((order > 1) && (order_a == 1)) {
            a = tag_linear_expression(a);
        }
        if ((order > 1) && (order_b == 1)) {
            b = tag_linear_expression(b);
        }

        return Div::make(a, b);
    }

    // For other binary operators, if either argument is non-constant, then the
    // whole expression is non-linear
    template<typename T>
    Expr visit_binary(T *op) {

        Expr a = mutate(op->a);
        unsigned int order_a = order;
        Expr b = mutate(op->b);
        unsigned int order_b = order;

        if (order_a || order_b) {
            order = 2;
        }

        if ((order > 1) && (order_a == 1)) {
            a = tag_linear_expression(a);
        }
        if ((order > 1) && (order_b == 1)) {
            b = tag_linear_expression(b);
        }

        return T::make(a, b);
    }

    Expr visit(const Mod *op) override {
        return visit_binary(op);
    }

    // Break the expression into a piecewise function, if the expressions are
    // linear, we treat the piecewise behavior specially during codegen

    // Once this is done, Min and Max should call visit_binary_linear and the code
    // in setup_mesh will handle piecewise linear behavior introduced by these
    // expressions
    Expr visit(const Min *op) override {
        return visit_binary(op);
    }
    Expr visit(const Max *op) override {
        return visit_binary(op);
    }

    Expr visit(const EQ *op) override {
        return visit_binary(op);
    }
    Expr visit(const NE *op) override {
        return visit_binary(op);
    }
    Expr visit(const LT *op) override {
        return visit_binary(op);
    }
    Expr visit(const LE *op) override {
        return visit_binary(op);
    }
    Expr visit(const GT *op) override {
        return visit_binary(op);
    }
    Expr visit(const GE *op) override {
        return visit_binary(op);
    }
    Expr visit(const And *op) override {
        return visit_binary(op);
    }
    Expr visit(const Or *op) override {
        return visit_binary(op);
    }

    Expr visit(const Not *op) override {
        Expr a = mutate(op->a);
        unsigned int order_a = order;

        if (order_a) {
            order = 2;
        }

        return Not::make(a);
    }

    Expr visit(const Broadcast *op) override {
        Expr a = mutate(op->value);

        if (order == 1) {
            a = tag_linear_expression(a);
        }

        if (order) {
            order = 2;
        }

        return Broadcast::make(a, op->lanes);
    }

    Expr visit(const Select *op) override {

        // If either the true expression or the false expression is non-linear
        // in terms of the loop variables, then the select expression might
        // evaluate to a non-linear expression and is disqualified.

        // If both are either linear or constant, and the condition expression
        // is constant with respect to the loop variables, then either the true
        // or false expression will be evaluated across the whole loop domain,
        // and the select expression is linear. Otherwise, the expression is
        // disqualified.

        // The condition expression must be constant (order == 0) with respect
        // to the loop variables.
        Expr mutated_condition = mutate(op->condition);
        int condition_order = (order != 0) ? 2 : 0;

        Expr mutated_true_value = mutate(op->true_value);
        int true_value_order = order;

        Expr mutated_false_value = mutate(op->false_value);
        int false_value_order = order;

        order = std::max(std::max(condition_order, true_value_order), false_value_order);

        if ((order > 1) && (condition_order == 1)) {
            mutated_condition = tag_linear_expression(mutated_condition);
        }
        if ((order > 1) && (true_value_order == 1)) {
            mutated_true_value = tag_linear_expression(mutated_true_value);
        }
        if ((order > 1) && (false_value_order == 1)) {
            mutated_false_value = tag_linear_expression(mutated_false_value);
        }

        return Select::make(mutated_condition, mutated_true_value, mutated_false_value);
    }

public:
    std::vector<std::string> loop_vars;

    Scope<int> scope;

    unsigned int order;
    bool found;

    unsigned int total_found;

    // This parameter controls the maximum number of linearly varying
    // expressions halide will pull out of the fragment shader and evaluate per
    // vertex, and allow the GPU to linearly interpolate across the domain. For
    // OpenGL ES 2.0 we can pass 16 vec4 varying attributes, or 64 scalars. Two
    // scalar slots are used by boilerplate code to pass pixel coordinates.
    const unsigned int max_expressions;

    FindLinearExpressions()
        : in_glsl_loops(false), total_found(0), max_expressions(62) {
    }
};

Stmt find_linear_expressions(Stmt s) {

    return FindLinearExpressions().mutate(s);
}

// This visitor produces a map containing name and expression pairs from varying
// tagged intrinsics
class FindVaryingAttributeTags : public IRVisitor {
public:
    FindVaryingAttributeTags(std::map<std::string, Expr> &varyings_)
        : varyings(varyings_) {
    }

    using IRVisitor::visit;

    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::glsl_varying)) {
            std::string name = op->args[0].as<StringImm>()->value;
            varyings[name] = op->args[1];
        }
        IRVisitor::visit(op);
    }

    std::map<std::string, Expr> &varyings;
};

// This visitor removes glsl_varying intrinsics.
class RemoveVaryingAttributeTags : public IRMutator {
public:
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::glsl_varying)) {
            // Replace the call expression with its wrapped argument expression
            return op->args[1];
        } else {
            return IRMutator::visit(op);
        }
    }
};

Stmt remove_varying_attributes(Stmt s) {
    return RemoveVaryingAttributeTags().mutate(s);
}

// This visitor removes glsl_varying intrinsics and replaces them with
// variables. After this visitor is called, the varying attribute expressions
// will no longer appear in the IR tree, only variables with the .varying tag
// will remain.
class ReplaceVaryingAttributeTags : public IRMutator {
public:
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::glsl_varying)) {
            // Replace the intrinsic tag wrapper with a variable the variable
            // name ends with the tag ".varying"
            std::string name = op->args[0].as<StringImm>()->value;

            internal_assert(ends_with(name, ".varying"));

            return Variable::make(op->type, name);
        } else {
            return IRMutator::visit(op);
        }
    }
};

Stmt replace_varying_attributes(Stmt s) {
    return ReplaceVaryingAttributeTags().mutate(s);
}

// This visitor produces a set of variable names that are tagged with
// ".varying".
class FindVaryingAttributeVars : public IRVisitor {
public:
    using IRVisitor::visit;

    void visit(const Variable *op) override {
        if (ends_with(op->name, ".varying")) {
            variables.insert(op->name);
        }
    }

    std::set<std::string> variables;
};

// Remove varying attributes from the varying's map if they do not appear in the
// loop_stmt because they were simplified away.
void prune_varying_attributes(Stmt loop_stmt, std::map<std::string, Expr> &varying) {
    FindVaryingAttributeVars find;
    loop_stmt.accept(&find);

    std::vector<std::string> remove_list;

    for (const std::pair<std::string, Expr> &i : varying) {
        const std::string &name = i.first;
        if (find.variables.find(name) == find.variables.end()) {
            debug(2) << "Removed varying attribute " << name << "\n";
            remove_list.push_back(name);
        }
    }

    for (const std::string &i : remove_list) {
        varying.erase(i);
    }
}

// This visitor changes the type of variables tagged with .varying to float,
// since GLSL will only interpolate floats. In the case that the type of the
// varying attribute was integer, the interpolated float value is snapped to the
// integer grid and cast to the integer type. This case occurs with coordinate
// expressions where the integer loop variables are manipulated without being
// converted to floating point. In other cases, like an affine transformation of
// image coordinates, the loop variables are cast to floating point within the
// interpolated expression.
class CastVaryingVariables : public IRMutator {
protected:
    using IRMutator::visit;

    Expr visit(const Variable *op) override {
        if ((ends_with(op->name, ".varying")) && (op->type != Float(32))) {
            // The incoming variable will be float type because GLSL only
            // interpolates floats
            Expr v = Variable::make(Float(32), op->name);

            // If the varying attribute expression that this variable replaced
            // was integer type, snap the interpolated floating point variable
            // back to the integer grid.
            return Cast::make(op->type, floor(v + 0.5f));
        } else {
            // Otherwise, the variable keeps its float type.
            return op;
        }
    }
};

// This visitor casts the named variables to float, and then propagates the
// float type through the expression. The variable is offset by 0.5f
class CastVariablesToFloatAndOffset : public IRMutator {
protected:
    using IRMutator::visit;

    Expr visit(const Variable *op) override {

        // Check to see if the variable matches a loop variable name
        if (std::find(names.begin(), names.end(), op->name) != names.end()) {
            // This case is used by integer type loop variables. They are cast
            // to float and offset.
            return Expr(op) - 0.5f;

        } else if (scope.contains(op->name) && (op->type != scope.get(op->name).type())) {
            // Otherwise, check to see if it is defined by a modified let
            // expression and if so, change the type of the variable to match
            // the modified expression
            return Variable::make(scope.get(op->name).type(), op->name);
        } else {
            return op;
        }
    }

    Type float_type(Expr e) {
        return Float(e.type().bits(), e.type().lanes());
    }

    template<typename T>
    Expr visit_binary_op(const T *op) {
        Expr mutated_a = mutate(op->a);
        Expr mutated_b = mutate(op->b);

        bool a_float = mutated_a.type().is_float();
        bool b_float = mutated_b.type().is_float();

        // If either argument is a float, then make sure both are float
        if (a_float || b_float) {
            if (!a_float) {
                mutated_a = Cast::make(float_type(op->b), mutated_a);
            }
            if (!b_float) {
                mutated_b = Cast::make(float_type(op->a), mutated_b);
            }
        }

        return T::make(mutated_a, mutated_b);
    }

    Expr visit(const Add *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const Sub *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const Mul *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const Div *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const Mod *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const Min *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const Max *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const EQ *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const NE *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const LT *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const LE *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const GT *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const GE *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const And *op) override {
        return visit_binary_op(op);
    }
    Expr visit(const Or *op) override {
        return visit_binary_op(op);
    }

    Expr visit(const Select *op) override {
        Expr mutated_condition = mutate(op->condition);
        Expr mutated_true_value = mutate(op->true_value);
        Expr mutated_false_value = mutate(op->false_value);

        bool t_float = mutated_true_value.type().is_float();
        bool f_float = mutated_false_value.type().is_float();

        // If either argument is a float, then make sure both are float
        if (t_float || f_float) {
            if (!t_float) {
                mutated_true_value = Cast::make(float_type(op->true_value), mutated_true_value);
            }
            if (!f_float) {
                mutated_false_value = Cast::make(float_type(op->false_value), mutated_false_value);
            }
        }

        return Select::make(mutated_condition, mutated_true_value, mutated_false_value);
    }

    Expr visit(const Ramp *op) override {
        Expr mutated_base = mutate(op->base);
        Expr mutated_stride = mutate(op->stride);

        // If either base or stride is a float, then make sure both are float
        bool base_float = mutated_base.type().is_float();
        bool stride_float = mutated_stride.type().is_float();
        if (!base_float && stride_float) {
            mutated_base = Cast::make(float_type(op->base), mutated_base);
        } else if (base_float && !stride_float) {
            mutated_stride = Cast::make(float_type(op->stride), mutated_stride);
        }

        if (mutated_base.same_as(op->base) && mutated_stride.same_as(op->stride)) {
            return op;
        } else {
            return Ramp::make(mutated_base, mutated_stride, op->lanes);
        }
    }

    Expr visit(const Let *op) override {
        Expr mutated_value = mutate(op->value);

        bool changed = op->value.type().is_float() != mutated_value.type().is_float();
        if (changed) {
            scope.push(op->name, mutated_value);
        }

        Expr mutated_body = mutate(op->body);

        if (changed) {
            scope.pop(op->name);
        }

        return Let::make(op->name, mutated_value, mutated_body);
    }
    Stmt visit(const LetStmt *op) override {

        Expr mutated_value = mutate(op->value);

        bool changed = op->value.type().is_float() != mutated_value.type().is_float();
        if (changed) {
            scope.push(op->name, mutated_value);
        }

        Stmt mutated_body = mutate(op->body);

        if (changed) {
            scope.pop(op->name);
        }

        return LetStmt::make(op->name, mutated_value, mutated_body);
    }

public:
    CastVariablesToFloatAndOffset(const std::vector<std::string> &names_)
        : names(names_) {
    }

    const std::vector<std::string> &names;
    Scope<Expr> scope;
};

// This is the base class for a special mutator that, by default, turns an IR
// tree into a tree of Stmts. Derived classes overload visit methods to filter
// out specific expressions which are placed in Evaluate nodes within the new
// tree.  This functionality is used by GLSL varying attributes to transform
// tagged linear expressions into Store nodes for the vertex buffer. The
// IRFilter allows these expressions to be filtered out while maintaining the
// existing structure of Let variable scopes around them.
//
// TODO: could this be made to use the IRMutator pattern instead?
class IRFilter : public IRVisitor {
public:
    virtual Stmt mutate(const Expr &e);
    virtual Stmt mutate(const Stmt &s);

protected:
    using IRVisitor::visit;

    Stmt stmt;

    void visit(const IntImm *) override;
    void visit(const FloatImm *) override;
    void visit(const StringImm *) override;
    void visit(const Cast *) override;
    void visit(const Variable *) override;
    void visit(const Add *) override;
    void visit(const Sub *) override;
    void visit(const Mul *) override;
    void visit(const Div *) override;
    void visit(const Mod *) override;
    void visit(const Min *) override;
    void visit(const Max *) override;
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GT *) override;
    void visit(const GE *) override;
    void visit(const And *) override;
    void visit(const Or *) override;
    void visit(const Not *) override;
    void visit(const Select *) override;
    void visit(const Load *) override;
    void visit(const Ramp *) override;
    void visit(const Broadcast *) override;
    void visit(const Call *) override;
    void visit(const Let *) override;
    void visit(const LetStmt *) override;
    void visit(const AssertStmt *) override;
    void visit(const ProducerConsumer *) override;
    void visit(const For *) override;
    void visit(const Store *) override;
    void visit(const Provide *) override;
    void visit(const Allocate *) override;
    void visit(const Free *) override;
    void visit(const Realize *) override;
    void visit(const Block *) override;
    void visit(const IfThenElse *) override;
    void visit(const Evaluate *) override;
};

Stmt IRFilter::mutate(const Expr &e) {
    if (e.defined()) {
        e.accept(this);
    } else {
        stmt = Stmt();
    }
    return stmt;
}

Stmt IRFilter::mutate(const Stmt &s) {
    if (s.defined()) {
        s.accept(this);
    } else {
        stmt = Stmt();
    }
    return stmt;
}

namespace {
template<typename T, typename A>
void mutate_operator(IRFilter *mutator, const T *op, const A op_a, Stmt *stmt) {
    Stmt a = mutator->mutate(op_a);
    *stmt = a;
}
template<typename T, typename A, typename B>
void mutate_operator(IRFilter *mutator, const T *op, const A op_a, const B op_b, Stmt *stmt) {
    Stmt a = mutator->mutate(op_a);
    Stmt b = mutator->mutate(op_b);
    *stmt = make_block(a, b);
}
template<typename T, typename A, typename B, typename C>
void mutate_operator(IRFilter *mutator, const T *op, const A op_a, const B op_b, const C op_c, Stmt *stmt) {
    Stmt a = mutator->mutate(op_a);
    Stmt b = mutator->mutate(op_b);
    Stmt c = mutator->mutate(op_c);
    *stmt = make_block(make_block(a, b), c);
}
}  // namespace

void IRFilter::visit(const IntImm *op) {
    stmt = Stmt();
}
void IRFilter::visit(const FloatImm *op) {
    stmt = Stmt();
}
void IRFilter::visit(const StringImm *op) {
    stmt = Stmt();
}
void IRFilter::visit(const Variable *op) {
    stmt = Stmt();
}

void IRFilter::visit(const Cast *op) {
    mutate_operator(this, op, op->value, &stmt);
}

void IRFilter::visit(const Add *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const Sub *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const Mul *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const Div *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const Mod *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const Min *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const Max *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const EQ *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const NE *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const LT *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const LE *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const GT *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const GE *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const And *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}
void IRFilter::visit(const Or *op) {
    mutate_operator(this, op, op->a, op->b, &stmt);
}

void IRFilter::visit(const Not *op) {
    mutate_operator(this, op, op->a, &stmt);
}

void IRFilter::visit(const Select *op) {
    mutate_operator(this, op, op->condition, op->true_value, op->false_value, &stmt);
}

void IRFilter::visit(const Load *op) {
    mutate_operator(this, op, op->predicate, op->index, &stmt);
}

void IRFilter::visit(const Ramp *op) {
    mutate_operator(this, op, op->base, op->stride, &stmt);
}

void IRFilter::visit(const Broadcast *op) {
    mutate_operator(this, op, op->value, &stmt);
}

void IRFilter::visit(const Call *op) {
    std::vector<Stmt> new_args(op->args.size());

    // Mutate the args
    for (size_t i = 0; i < op->args.size(); i++) {
        Expr old_arg = op->args[i];
        Stmt new_arg = mutate(old_arg);
        new_args[i] = new_arg;
    }

    stmt = Stmt();
    for (size_t i = 0; i < new_args.size(); ++i) {
        if (new_args[i].defined()) {
            stmt = make_block(new_args[i], stmt);
        }
    }
}

void IRFilter::visit(const Let *op) {
    mutate_operator(this, op, op->value, op->body, &stmt);
}

void IRFilter::visit(const LetStmt *op) {
    mutate_operator(this, op, op->value, op->body, &stmt);
}

void IRFilter::visit(const AssertStmt *op) {
    mutate_operator(this, op, op->condition, op->message, &stmt);
}

void IRFilter::visit(const ProducerConsumer *op) {
    mutate_operator(this, op, op->body, &stmt);
}

void IRFilter::visit(const For *op) {
    mutate_operator(this, op, op->min, op->extent, op->body, &stmt);
}

void IRFilter::visit(const Store *op) {
    mutate_operator(this, op, op->predicate, op->value, op->index, &stmt);
}

void IRFilter::visit(const Provide *op) {
    stmt = Stmt();
    for (size_t i = 0; i < op->args.size(); i++) {
        Stmt new_arg = mutate(op->args[i]);
        if (new_arg.defined()) {
            stmt = make_block(new_arg, stmt);
        }
        Stmt new_value = mutate(op->values[i]);
        if (new_value.defined()) {
            stmt = make_block(new_value, stmt);
        }
    }
}

void IRFilter::visit(const Allocate *op) {
    stmt = Stmt();
    for (size_t i = 0; i < op->extents.size(); i++) {
        Stmt new_extent = mutate(op->extents[i]);
        if (new_extent.defined())
            stmt = make_block(new_extent, stmt);
    }

    Stmt body = mutate(op->body);
    if (body.defined())
        stmt = make_block(body, stmt);

    Stmt condition = mutate(op->condition);
    if (condition.defined())
        stmt = make_block(condition, stmt);
}

void IRFilter::visit(const Free *op) {
}

void IRFilter::visit(const Realize *op) {
    stmt = Stmt();

    // Mutate the bounds
    for (size_t i = 0; i < op->bounds.size(); i++) {
        Expr old_min = op->bounds[i].min;
        Expr old_extent = op->bounds[i].extent;
        Stmt new_min = mutate(old_min);
        Stmt new_extent = mutate(old_extent);

        if (new_min.defined())
            stmt = make_block(new_min, stmt);
        if (new_extent.defined())
            stmt = make_block(new_extent, stmt);
    }

    Stmt body = mutate(op->body);
    if (body.defined())
        stmt = make_block(body, stmt);

    Stmt condition = mutate(op->condition);
    if (condition.defined())
        stmt = make_block(condition, stmt);
}

void IRFilter::visit(const Block *op) {
    mutate_operator(this, op, op->first, op->rest, &stmt);
}

void IRFilter::visit(const IfThenElse *op) {
    mutate_operator(this, op, op->condition, op->then_case, op->else_case, &stmt);
}

void IRFilter::visit(const Evaluate *op) {
    mutate_operator(this, op, op->value, &stmt);
}

// This visitor takes a IR tree containing a set of .glsl scheduled for-loops
// and creates a matching set of serial for-loops to setup a vertex buffer on
// the  host. The visitor  filters out glsl_varying intrinsics and transforms
// them into Store nodes to evaluate the linear expressions they tag within the
// scope of all of the Let definitions they fall within.
// The statement returned by this operation should be executed on the host
// before the call to halide_dev_run.
class CreateVertexBufferOnHost : public IRFilter {
public:
    using IRFilter::visit;

    void visit(const Call *op) override {

        // Transform glsl_varying intrinsics into store operations to output the
        // vertex coordinate values.
        if (op->is_intrinsic(Call::glsl_varying)) {

            // Construct an expression for the offset of the coordinate value in
            // terms of the current integer loop variables and the varying
            // attribute channel number
            std::string attribute_name = op->args[0].as<StringImm>()->value;

            Expr offset_expression = Variable::make(Int(32), "gpu.vertex_offset") +
                                     attribute_order[attribute_name];

            stmt = Store::make(vertex_buffer_name, op->args[1], offset_expression,
                               Parameter(), const_true(op->args[1].type().lanes()), ModulusRemainder());
        } else {
            IRFilter::visit(op);
        }
    }

    void visit(const Let *op) override {
        stmt = nullptr;

        Stmt mutated_value = mutate(op->value);
        Stmt mutated_body = mutate(op->body);

        // If an operation was filtered out of the body, also filter out the
        // whole let expression so that the body may be evaluated completely. In
        // the case that the let variable is not used in the mutated body, it
        // will be removed by simplification.
        if (mutated_body.defined()) {
            stmt = LetStmt::make(op->name, op->value, mutated_body);
        }

        // If an operation with a side effect was filtered out of the value, the
        // stmt'ified value is placed in a Block, so that the side effect will
        // be included in filtered IR tree.
        if (mutated_value.defined()) {
            stmt = make_block(mutated_value, stmt);
        }
    }

    void visit(const LetStmt *op) override {
        stmt = Stmt();

        Stmt mutated_value = mutate(op->value);
        Stmt mutated_body = mutate(op->body);

        if (mutated_body.defined()) {
            stmt = LetStmt::make(op->name, op->value, mutated_body);
        }

        if (mutated_value.defined()) {
            stmt = make_block(mutated_value, stmt);
        }
    }

    void visit(const For *op) override {
        if (CodeGen_GPU_Dev::is_gpu_var(op->name) && op->device_api == DeviceAPI::GLSL) {
            // Create a for-loop of integers iterating over the coordinates in
            // this dimension

            std::string name = op->name + ".idx";
            const std::vector<Expr> &dim = dims[op->name];

            internal_assert(for_loops.size() <= 1);
            for_loops.push_back(op);

            Expr loop_variable = Variable::make(Int(32), name);
            loop_variables.push_back(loop_variable);

            // TODO: When support for piecewise linear expressions is added this
            // expression must support more than two coordinates in each
            // dimension.
            Expr coord_expr = select(loop_variable == 0, dim[0], dim[1]);

            // Visit the body of the for-loop
            Stmt mutated_body = mutate(op->body);

            // If this was the inner most for-loop of the .glsl scheduled pair,
            // add a let definition for the vertex index and Store the spatial
            // coordinates
            const For *nested_for = op->body.as<For>();
            if (!(nested_for && CodeGen_GPU_Dev::is_gpu_var(nested_for->name))) {

                // Create a variable to store the offset in floats of this
                // vertex
                Expr gpu_varying_offset = Variable::make(Int(32), "gpu.vertex_offset");

                // Add expressions for the x and y vertex coordinates.
                Expr coord1 = cast<float>(Variable::make(Int(32), for_loops[0]->name));
                Expr coord0 = cast<float>(Variable::make(Int(32), for_loops[1]->name));

                // Transform the vertex coordinates to GPU device coordinates on
                // [-1,1]
                coord1 = (coord1 / for_loops[0]->extent) * 2.0f - 1.0f;
                coord0 = (coord0 / for_loops[1]->extent) * 2.0f - 1.0f;

                // Remove varying attribute intrinsics from the vertex setup IR
                // tree.
                mutated_body = remove_varying_attributes(mutated_body);

                // The GPU will take texture coordinates at pixel centers during
                // interpolation, we offset the Halide integer grid by 0.5 so that
                // these coordinates line up on integer coordinate values.
                std::vector<std::string> names = {for_loops[0]->name, for_loops[1]->name};
                CastVariablesToFloatAndOffset cast_and_offset(names);
                mutated_body = cast_and_offset.mutate(mutated_body);

                // Store the coordinates into the vertex buffer in interleaved
                // order
                mutated_body = make_block(Store::make(vertex_buffer_name,
                                                      coord1,
                                                      gpu_varying_offset + 1,
                                                      Parameter(), const_true(),
                                                      ModulusRemainder()),
                                          mutated_body);

                mutated_body = make_block(Store::make(vertex_buffer_name,
                                                      coord0,
                                                      gpu_varying_offset + 0,
                                                      Parameter(), const_true(),
                                                      ModulusRemainder()),
                                          mutated_body);

                // TODO: The value 2 in this expression must be changed to reflect
                // addition coordinate values in the fastest changing dimension when
                // support for piecewise linear functions is added
                Expr offset_expression = (loop_variables[0] * num_padded_attributes * 2) +
                                         (loop_variables[1] * num_padded_attributes);
                mutated_body = LetStmt::make("gpu.vertex_offset",
                                             offset_expression, mutated_body);
            }

            // Add a let statement for the for-loop name variable
            Stmt loop_var = LetStmt::make(op->name, coord_expr, mutated_body);

            stmt = For::make(name, 0, (int)dim.size(), ForType::Serial, DeviceAPI::None, loop_var);

        } else {
            IRFilter::visit(op);
        }
    }

    // The name of the previously allocated vertex buffer to store values
    std::string vertex_buffer_name;

    // Expressions for the spatial values of each coordinate in the GPU scheduled
    // loop dimensions.
    typedef std::map<std::string, std::vector<Expr>> DimsType;
    DimsType dims;

    // The channel of each varying attribute in the interleaved vertex buffer
    std::map<std::string, int> attribute_order;

    // The number of attributes padded up to the next multiple of four. This is
    // the stride from one vertex to the next in the buffer
    int num_padded_attributes;

    // Independent variable names in the linear expressions
    std::vector<const For *> for_loops;

    // Loop variables iterated across per GPU scheduled loop dimension to
    // construct the vertex buffer
    std::vector<Expr> loop_variables;
};

// These two methods provide a workaround to maintain unused let statements in
// the IR tree util calls are added that used them in codegen.

// TODO: We want to define a set of variables during lowering, and then use
// them during GLSL host codegen to pass values to the
// halide_dev_run function. It turns out that these variables will
// be simplified away since the call to the function does not appear
// in the IR. To avoid this we wrap the declaration in a
// return_second intrinsic as well as add a return_second intrinsic
// to consume the value.
// This prevents simplification passes that occur before codegen
// from removing the variables or substituting in their constant
// values.

Expr dont_simplify(Expr v_) {
    return Internal::Call::make(v_.type(),
                                Internal::Call::return_second,
                                {0, v_},
                                Internal::Call::Intrinsic);
}

Stmt used_in_codegen(Type type_, const std::string &v_) {
    return Evaluate::make(Internal::Call::make(Int(32),
                                               Internal::Call::return_second,
                                               {Variable::make(type_, v_), 0},
                                               Internal::Call::Intrinsic));
}

// This mutator inserts a set of serial for-loops to create the vertex buffer
// on the host using CreateVertexBufferOnHost above.
class CreateVertexBufferHostLoops : public IRMutator {
public:
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        if (CodeGen_GPU_Dev::is_gpu_var(op->name) && op->device_api == DeviceAPI::GLSL) {

            const For *loop1 = op;
            const For *loop0 = loop1->body.as<For>();

            internal_assert(loop1->body.as<For>()) << "Did not find pair of nested For loops";

            // Construct a mesh of expressions to instantiate during runtime
            std::map<std::string, Expr> varyings;

            FindVaryingAttributeTags tag_finder(varyings);
            op->accept(&tag_finder);

            // Establish and order for the attributes in each vertex
            std::map<std::string, int> attribute_order;

            // Add the attribute names to the mesh in the order that they appear in
            // each vertex
            attribute_order["__vertex_x"] = 0;
            attribute_order["__vertex_y"] = 1;

            int idx = 2;
            for (const std::pair<std::string, Expr> &v : varyings) {
                attribute_order[v.first] = idx++;
            }

            // Construct a list of expressions giving to coordinate locations along
            // each dimension, starting with the minimum and maximum coordinates

            attribute_order[loop0->name] = 0;
            attribute_order[loop1->name] = 1;

            Expr loop0_max = Add::make(loop0->min, loop0->extent);
            Expr loop1_max = Add::make(loop1->min, loop1->extent);

            std::vector<std::vector<Expr>> coords(2);

            coords[0].push_back(loop0->min);
            coords[0].push_back(loop0_max);

            coords[1].push_back(loop1->min);
            coords[1].push_back(loop1_max);

            // Count the two spatial x and y coordinates plus the number of
            // varying attribute expressions found
            int num_attributes = varyings.size() + 2;

            // Pad the number of attributes up to a multiple of four
            int num_padded_attributes = (num_attributes + 0x3) & ~0x3;
            int vertex_buffer_size = num_padded_attributes * coords[0].size() * coords[1].size();

            // Filter out varying attribute expressions from the glsl scheduled
            // loops. The expressions are filtered out in situ, among the
            // variables in scope
            CreateVertexBufferOnHost vs;
            vs.vertex_buffer_name = "glsl.vertex_buffer";
            vs.num_padded_attributes = num_padded_attributes;
            vs.dims[loop0->name] = coords[0];
            vs.dims[loop1->name] = coords[1];
            vs.attribute_order = attribute_order;

            Stmt vertex_setup = vs.mutate(loop1);

            // Remove varying attribute intrinsics from the vertex setup IR
            // tree. These may occur if an expression such as a Let-value was
            // filtered out without being mutated.
            vertex_setup = remove_varying_attributes(vertex_setup);

            // Simplify the new host code.  Workaround for #588
            vertex_setup = simplify(vertex_setup);
            vertex_setup = simplify(vertex_setup);
            vertex_setup = simplify(vertex_setup);
            vertex_setup = simplify(vertex_setup);

            // Replace varying attribute intriniscs in the gpu scheduled loops
            // with variables with ".varying" tagged names
            Stmt loop_stmt = replace_varying_attributes(op);

            // Simplify
            loop_stmt = simplify(loop_stmt, true);

            // It is possible that linear expressions we tagged in higher-level
            // intrinsics were removed by simplification if they were only used in
            // subsequent tagged linear expressions. Run a pass to check for
            // these and remove them from the varying attribute list
            prune_varying_attributes(loop_stmt, varyings);

            // At this point the varying attribute expressions have been removed from
            // loop_stmt- it only contains variables tagged with .varying

            // The GPU will only interpolate floating point values so the varying
            // attribute variables must be converted to floating point. If the
            // original varying expression was integer, casts are inserts to
            // snap the value back to the integer grid.
            loop_stmt = CastVaryingVariables().mutate(loop_stmt);

            // Insert two new for-loops for vertex buffer generation on the host
            // before the two GPU scheduled for-loops
            return LetStmt::make("glsl.num_coords_dim0", dont_simplify((int)(coords[0].size())),
                                 LetStmt::make("glsl.num_coords_dim1", dont_simplify((int)(coords[1].size())),
                                               LetStmt::make("glsl.num_padded_attributes", dont_simplify(num_padded_attributes),
                                                             Allocate::make(vs.vertex_buffer_name, Float(32), MemoryType::Auto, {vertex_buffer_size}, const_true(),
                                                                            Block::make(vertex_setup,
                                                                                        Block::make(loop_stmt,
                                                                                                    Block::make(used_in_codegen(Int(32), "glsl.num_coords_dim0"),
                                                                                                                Block::make(used_in_codegen(Int(32), "glsl.num_coords_dim1"),
                                                                                                                            Block::make(used_in_codegen(Int(32), "glsl.num_padded_attributes"),
                                                                                                                                        Free::make(vs.vertex_buffer_name))))))))));
        } else {
            return IRMutator::visit(op);
        }
    }
};

Stmt setup_gpu_vertex_buffer(Stmt s) {
    CreateVertexBufferHostLoops vb;
    return vb.mutate(s);
}

}  // namespace Internal
}  // namespace Halide
