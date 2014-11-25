#include "VaryingAttributes.h"

#include <algorithm>

#include "CodeGen_GPU_Dev.h"

#include "IRMutator.h"
#include "IRMatch.h"
#include "CSE.h"
#include "Simplify.h"

// TODO:(abstephensg) Need to integrate with specialize_branched_loops branch
// #include "LinearSolve.h"

namespace Halide {
namespace Internal {
    
// This mutator substitutes out variables and corresponding let expressions. No
// new variables are added to the scope passed to the visitor when it is
// created, only lets outside of the visited expression are substituted.
class ExternalUnletify : public IRMutator {
public:
    using IRMutator::visit;
    virtual void visit(const Variable *op) {
        if (scope.contains(op->name)) {
            expr = mutate(scope.get(op->name));
        } else {
            expr = op;
        }
    }
    virtual void visit(const Call *op) {
        if (op->name == Call::glsl_varying) {
            // Unwrap the the intrinsic from the tagged expression
            expr = mutate(op->args[1]);
        } else {
            IRMutator::visit(op);
        }
    }

    ExternalUnletify(Scope<Expr>& scope_) { scope.set_containing_scope(&scope_); }
    
    Scope<Expr> scope;
};
    
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
    
    Expr tag_linear_expression(Expr e, const std::string& name = unique_name('a')) {

        internal_assert(name.length() > 0);

        if (total_found >= max_expressions) {
            return e;
        }

        // Wrap the expression with an intrinsic to tag that it is a varying
        // attribute. These tagged variables will be pulled out of the fragment
        // shader during a subsequent pass
        Expr intrinsic = Call::make(e.type(), Call::glsl_varying,
                                    vec(Expr(name + ".varying"),e),
                                    Call::Intrinsic);
        ++total_found;
        
        return intrinsic;
    }
    
    virtual void visit(const Call *op) {
        
        std::vector<Expr> new_args = op->args;
        
        // Check to see if this call is a load
        if (op->name == Call::glsl_texture_load) {
            // Check if the texture coordinate arguments are linear wrt the GPU
            // loop variables
            internal_assert(loop_vars.size() > 0) << "No GPU loop variables found at texture load";
            
            // Iterate over the texture coordinate arguments
            for (int i=0;i!=2;++i) {
                new_args[2+i] = mutate(op->args[2+i]);
                if (order == 1) {
                    new_args[2+i] = tag_linear_expression(new_args[2+i]);
                }
            }
        } else if (op->name == Call::glsl_texture_store) {
            // Check if the value expression is linear wrt the loop variables
            internal_assert(loop_vars.size() > 0) << "No GPU loop variables found at texture load";
            
            // The value is the 5th argument to the intrinsic
            new_args[5] = mutate(new_args[5]);
            if (order == 1) {
                new_args[5] = tag_linear_expression(new_args[5]);
            }
        }

        // The texture lookup itself is counted as a non-linear operation
        order = 2;
        expr = Call::make(op->type, op->name, new_args, op->call_type,
                          op->func, op->value_index, op->image, op->param);
    }
    
    virtual void visit(const Let *op) {

        Expr mutated_value = mutate(op->value);
        int value_order = order;
        
        scope.push(op->name, order);
        
        Expr mutated_body = mutate(op->body);
        
        if ((value_order == 1) && (total_found < max_expressions)) {
            // Wrap the let value with a varying tag
            mutated_value = Call::make(mutated_value.type(), Call::glsl_varying,
                              vec<Expr>(op->name + ".varying",mutated_value),
                              Call::Intrinsic);
            ++total_found;
        }

        expr = Let::make(op->name,mutated_value,mutated_body);

        scope.pop(op->name);
    }
    
    virtual void visit(const For *op) {
        // Check if the loop variable is a GPU variable thread variable
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            loop_vars.push_back(op->name);
        }
        
        Stmt mutated_body = mutate(op->body);
        
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            loop_vars.pop_back();
        }
        
        stmt = For::make(op->name, op->min, op->extent, op->for_type, mutated_body);
    }
    
    virtual void visit(const Variable *op) {
        if (std::find(loop_vars.begin(),loop_vars.end(),op->name) != loop_vars.end()) {
            order = 1;
        } else if (scope.contains(op->name)) {
            order = scope.get(op->name);
        } else {
            // If the variable is not found in scope, then we assume it is
            // constant in terms of the independent variables.
            order = 0;
        }
        expr = op;
    }
    
    template<typename T>
    void visit_imm(T* op) {
        order = 0;
        expr = T::make(op->value);
    }
    
    virtual void visit(const IntImm *op)    { visit_imm(op); }
    virtual void visit(const FloatImm *op)  { visit_imm(op); }
    virtual void visit(const StringImm *op) { visit_imm(op); }
    
    virtual void visit(const Cast *op) {
        
        Expr mutated_value = mutate(op->value);
        int value_order = order;
        
        // We can only interpolate float values, disqualify the expression if
        // this is a cast to a different type
        if (op->type.code != Type::Float) {
            order = 2;
        }
        
        if ((order > 1) && (value_order == 1)) {
            mutated_value = tag_linear_expression(mutated_value);
        }
        
        expr = Cast::make(op->type, mutated_value);
    }
    
    // Add and subtract do not make the expression non-linear, if it is already
    // linear or constant
    template<typename T>
    void visit_binary_linear(T* op) {
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
        
        expr = T::make(a,b);
    }
    
    virtual void visit(const Add *op) { visit_binary_linear(op); }
    virtual void visit(const Sub *op) { visit_binary_linear(op); }
    
    // Multiplying increases the order of the expression, possibly making it
    // non-linear
    virtual void visit(const Mul *op) {
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
        
        expr = Mul::make(a,b);
    }
    
    // Dividing is either multiplying by a constant, or makes the result
    // non-linear (i.e. order -1)
    virtual void visit(const Div *op) {
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
        
        expr = Div::make(a, b);
    }
    
    // For other binary operators, if either argument is non-constant, then the
    // whole expression is non-linear
    template<typename T>
    void visit_binary(T* op) {
        
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
        
        expr = T::make(a,b);
    }
    
    virtual void visit(const Mod *op) { visit_binary(op); }
    
    // Break the expression into a piecewise function, if the expressions are
    // linear, we treat the piecewise behavior specially during codegen
    
    // TODO:(abstephensg) Need to integrate with specialize_branched_loops branch
    
    // Once this is done, Min and Max should call visit_binary_linear and the code
    // in setup_mesh will handle piecewise linear behavior introduced by these
    // expressions
    virtual void visit(const Min *op) { visit_binary(op); }
    virtual void visit(const Max *op) { visit_binary(op); }
    
    virtual void visit(const EQ *op) { visit_binary(op); }
    virtual void visit(const NE *op) { visit_binary(op); }
    virtual void visit(const LT *op) { visit_binary(op); }
    virtual void visit(const LE *op) { visit_binary(op); }
    virtual void visit(const GT *op) { visit_binary(op); }
    virtual void visit(const GE *op) { visit_binary(op); }
    virtual void visit(const And *op) { visit_binary(op); }
    virtual void visit(const Or *op) { visit_binary(op); }
    
    virtual void visit(const Not *op) {
        Expr a = mutate(op->a);
        unsigned int order_a = order;
        
        if (order_a) {
            order = 2;
        }
        
        expr = Not::make(a);
    }
    
    virtual void visit(const Broadcast *op) {
        Expr a = mutate(op->value);
        
        if (order == 1) {
            a = tag_linear_expression(a);
        }
        
        if (order) {
            order = 2;
        }
        
        expr = Broadcast::make(a, op->width);
    }
    
    virtual void visit(const Select *op) {
        Expr mutated_condition = mutate(op->condition);
        int condition_order = order;
        
        Expr mutated_true_value = mutate(op->true_value);
        int true_value_order = order;

        Expr mutated_false_value = mutate(op->false_value);
        int false_value_order = order;
        
        order = std::max(std::max(condition_order,true_value_order),false_value_order);
        
        if ((order > 1) && (condition_order == 1)) {
            mutated_condition = tag_linear_expression(mutated_condition);
        }
        if ((order > 1) && (true_value_order == 1)) {
            mutated_true_value = tag_linear_expression(mutated_true_value);
        }
        if ((order > 1) && (false_value_order == 1)) {
            mutated_false_value = tag_linear_expression(mutated_false_value);
        }
        
        expr = Select::make(mutated_condition, mutated_true_value, mutated_false_value);
    }
    virtual void visit(const IfThenElse *op) {
        Expr mutated_condition = mutate(op->condition);
        int condition_order = order;
        
        Stmt mutated_then_case = mutate(op->then_case);
        int then_case_order = order;
        
        Stmt mutated_else_case = mutate(op->else_case);
        int else_case_order = order;
        
        order = std::max(std::max(condition_order,then_case_order),else_case_order);
        
        stmt = IfThenElse::make(mutated_condition,mutated_then_case,mutated_else_case);
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

    FindLinearExpressions() : total_found(0), max_expressions(62) {}
};

Stmt find_linear_expressions(Stmt s) {
    
    return FindLinearExpressions().mutate(s);
}



// Produce an expression containing a traversal through the branches in an IR
// tree based on a boolean vector of branch choices. For each binary branch
// encountered, the boolean value indicates which expression is included in the
// result.
class TraverseBranches : public IRMutator
{
public:
    using IRMutator::visit;

    bool which() {
        if (depth == (int)branch.size()) {
            branch.push_back(0);
        }
        return branch[depth];
    }
    
    template<typename T>
    void push(T e) {
        ++depth;
        expr = mutate(e);
        --depth;
    }
    
    template<typename T>
    void visit_binary_piecewise(const T* op) {
        if (which() == 0) {
            push(op->a);
        } else {
            push(op->b);
        }
    }
    
    virtual void visit(const Min *op) { visit_binary_piecewise(op); }
    virtual void visit(const Max *op) { visit_binary_piecewise(op); }
    
    TraverseBranches(std::vector<bool> traversal_) : branch(traversal_), depth(0) { }
    
    std::vector<bool> branch;
    int depth;
};
    
// This visitor traverses the IR tree to find vectors of branch choices to
//  supply to TraverseBranches.
class EnumerateBranches : public IRVisitor {
public:
    using IRVisitor::visit;
    
    // Branch node traversal
    template<typename T>
    void push(T e) {
        ++depth;
        e.accept(this);
        --depth;
    }

    // Tree leaves
    void visit_leaf() {
        Expr e = TraverseBranches(traversal).mutate(root);
        result.push_back(e);
    }

    // Visit a binary piecewise operation like min/max
    template<typename T>
    void visit_binary_piecewise(const T* op) {
        
        // Traverse the first branch expression
        traversal.push_back(0);
        count = 0;
        push(op->a);
        
        // Check if there are no branch expressions below this node, if so,
        // use the traversal vector to traverse the tree through this node and
        // produce a complete expression
        int count_a = count;
        if (!count) {
            visit_leaf();
        }
        
        // Traverse the second branch expression
        *traversal.rbegin() = 1;
        count = 0;
        push(op->b);
        int count_b = count;
        if (!count) {
            visit_leaf();
        }
        
        traversal.pop_back();
        
        // Keep track of the number of branch expressions found in this sub-tree
        count = std::max(count_a,count_b)+1;
    }
    
    virtual void visit(const Min *op) { visit_binary_piecewise(op); }
    virtual void visit(const Max *op) { visit_binary_piecewise(op); }
    
    std::vector<Expr> result;
    Expr root;
    
    std::vector<bool> traversal;
    int depth;
    int count;

    EnumerateBranches() : depth(0), count(0) {}
};

// This function returns a vector containing an expression for each possible
// branch within the specified expression. If the expression does not contain
// any branches, an empty vector is returned.
std::vector<Expr> enumerate_branches(Expr root) {
    EnumerateBranches e;
    e.root = root;
    root.accept(&e);
    
    return e.result;
}

// This visitor sets it's found flag to true if the IR tree contains a variable
// with the specified name, it also returns a pointer to the variable.
class ContainsVariable : public IRVisitor
{
public:
    using IRVisitor::visit;
    
    virtual void visit(const Variable *op) {
        if (op->name == name) {
            found = true;
            result = op;
        }
    }
    
    ContainsVariable(const std::string& name_) : found(false), result(NULL), name(name_) { }
    
    bool found;
    const Variable* result;
    std::string name;
};
    
bool contains_variable(Expr e, const std::string& name)
{
    ContainsVariable c(name);
    e.accept(&c);
    
    return c.found;
}
    
Type type_of_variable(Expr e, const std::string& name)
{
    ContainsVariable c(name);
    e.accept(&c);
    
    internal_assert(c.found) << "Expression " << e
                             << " does not contain variable " << name << "\n";
    
    return c.result->type;
}

// This visitor produces a map containing name and expression pairs from varying
// tagged intrinsics
class FindVaryingAttributeLets : public IRVisitor
{
public:
    FindVaryingAttributeLets(std::map<std::string,Expr>& varyings_) : varyings(varyings_) { }
    
    using IRVisitor::visit;

    virtual void visit(const Call *op) {
        if (op->name == Call::glsl_varying) {
            // Unletify the expression so that it can be moved outside of the
            // GPU For-loops and depend only on parameters
            std::string name = op->args[0].as<StringImm>()->value;
            Expr value = op->args[1];
            varyings[name] = ExternalUnletify(scope).mutate(value);
            debug(1) << varyings[name] << "\n";
        }
        IRVisitor::visit(op);
    }

    virtual void visit(const Let *op) {
        scope.push(op->name, op->value);
        IRVisitor::visit(op);
        scope.pop(op->name);
    }
    
    virtual void visit(const LetStmt *op) {
        scope.push(op->name, op->value);
        IRVisitor::visit(op);
        scope.pop(op->name);
    }
    
    Scope<Expr> scope;

    std::map<std::string,Expr>& varyings;
};

// This visitor removes  After this visitor is called, the varying attribute
// expressions will no longer appear in the tree, only variables with the
// .varying tag will remain.
class RemoveVaryingAttributeLets : public IRMutator {
public:
    using IRMutator::visit;

    virtual void visit(const Call *op) {
        if (op->name == Call::glsl_varying) {
            // Replace the intrinsic tag wrapper with a variable the variable
            // name ends with the tag ".varying"
            std::string name = op->args[0].as<StringImm>()->value;

            internal_assert(ends_with(name, ".varying"));

            expr = Variable::make(op->type, name);
        } else {
            IRMutator::visit(op);
        }
    }
};

Stmt remove_varying_attributes(Stmt s)
{
    return RemoveVaryingAttributeLets().mutate(s);
}

// This visitor produces a set of variable names that are tagged with
// ".varying", it is run after
class FindVaryingAttributes : public IRVisitor {
public:
    using IRVisitor::visit;
    
    virtual void visit(const Variable* op) {
        if (ends_with(op->name, ".varying")) {
            variables.insert(op->name);
        }
    }
    
    std::set<std::string> variables;
};

// Remove varying attributes from the varying's map if they do not appear in the
// loop_stmt because they were simplified away.
void prune_varying_attributes(Stmt loop_stmt, std::map<std::string,Expr>& varying)
{
    FindVaryingAttributes find;
    loop_stmt.accept(&find);
    
    std::vector<std::string> remove_list;

    for (std::map<std::string,Expr>::iterator i = varying.begin(); i != varying.end(); ++i) {
        const std::string& name = i->first;
        if (find.variables.find(name) == find.variables.end()) {
            debug(2) << "Removed varying attribute " << name << "\n";
            remove_list.push_back(name);
        }
    }
    
    for (std::vector<std::string>::iterator name = remove_list.begin(); name != remove_list.end(); ++name) {
        varying.erase(*name);
    }
}
    
// This visitor changes the type of variables tagged with .varying to float, and
// then propagates the float expression up through the expression, casting where
// necessary.
class CastVaryingVariablesToFloat : public IRMutator {
public:
    virtual void visit(const Variable *op) {
        if ((ends_with(op->name, ".varying")) && (op->type != Float(32))) {
            expr = Cast::make(Float(32),op);
        } else {
            expr = op;
        }
    }
    
    Type float_type(Expr e) {
        return Float(e.type().bits,e.type().width);
    }
    
    template<typename T>
    void visit_binary_op(const T* op) {
        Expr mutated_a = mutate(op->a);
        Expr mutated_b = mutate(op->b);
        
        bool a_float = mutated_a.type().is_float();
        bool b_float = mutated_b.type().is_float();
        
        if ((a_float || b_float) && (a_float != b_float)) {
            if (a_float) {
                mutated_b = Cast::make(float_type(op->a), mutated_b);
            } else {
                mutated_a = Cast::make(float_type(op->b), mutated_a);
            }
        }
        expr = T::make(mutated_a,mutated_b);
    }
    
    virtual void visit(const Add *op) { visit_binary_op(op); }
    virtual void visit(const Sub *op) { visit_binary_op(op); }
    virtual void visit(const Mul *op) { visit_binary_op(op); }
    virtual void visit(const Div *op) { visit_binary_op(op); }
    virtual void visit(const Mod *op) { visit_binary_op(op); }
    virtual void visit(const Min *op) { visit_binary_op(op); }
    virtual void visit(const Max *op) { visit_binary_op(op); }
    virtual void visit(const EQ *op) { visit_binary_op(op); }
    virtual void visit(const NE *op) { visit_binary_op(op); }
    virtual void visit(const LT *op) { visit_binary_op(op); }
    virtual void visit(const LE *op) { visit_binary_op(op); }
    virtual void visit(const GT *op) { visit_binary_op(op); }
    virtual void visit(const GE *op) { visit_binary_op(op); }
    virtual void visit(const And *op) { visit_binary_op(op); }
    virtual void visit(const Or *op) { visit_binary_op(op); }
    
    virtual void visit(const Select *op)  {
        Expr mutated_true_value = mutate(op->true_value);
        Expr mutated_false_value = mutate(op->false_value);
        
        bool t_float = mutated_true_value.type().is_float();
        bool f_float = mutated_false_value.type().is_float();
        
        if ((t_float || f_float) && !(t_float && f_float)) {
            if (!t_float)
                mutated_true_value = Cast::make(float_type(op->true_value), mutated_true_value);
            if (!f_float)
                mutated_false_value = Cast::make(float_type(op->false_value), mutated_false_value);
        }
        
        expr = Select::make(op->condition, mutated_true_value, mutated_false_value);
    }
};

// This visitor casts the named variables to float, and then propagates the
// float type through the expression. The variable is offset by 0.5f
class CastVariablesToFloatAndOffset : public CastVaryingVariablesToFloat {
public:
    virtual void visit(const Variable *op) {
        
        if (std::find(names.begin(), names.end(), op->name) != names.end()) {
            expr = Sub::make(Cast::make(float_type(op),op),0.5f);
        } else {
            expr = op;
        }
    }
    
    CastVariablesToFloatAndOffset(const std::vector<std::string>& names_) : names(names_) { }
    
    const std::vector<std::string>& names;
};

Stmt setup_mesh(const For* op, ExpressionMesh& result, std::map<std::string,Expr>& varyings)
{
    const For* loop1 = op;
    const For* loop0 = loop1->body.as<For>();
    
    internal_assert(loop1->body.as<For>()) << "Did not find pair of nested For loops";

    // Construct a mesh of expressions to instantiate during runtime
    FindVaryingAttributeLets tag_finder(varyings);
    op->accept(&tag_finder);

    // Remove the varying attribute let expressions from the statement
    Stmt loop_stmt = remove_varying_attributes(op);
    
    // Perform the Let-simplification pass that was skipped during
    // Lowering
    loop_stmt = simplify(loop_stmt,true);
    
    // It is possible that linear expressions we tagged in higher-level
    // intrinsics were removed by simplification if they were only used in
    // subsequent tagged linear expressions. Run a pass to check for
    // these and remove them from the varying attribute list
    prune_varying_attributes(loop_stmt,varyings);
    
    // At this point the varying attribute expressions have been removed from
    // loop_stmt- it only contains variables tagged with .varying
    
    // The GPU will only interpolate floating point values so the varying
    // attribute expression must be converted to floating point.
    loop_stmt = CastVaryingVariablesToFloat().mutate(loop_stmt);
    
    // The GPU will take texture coordinates at pixel centers during
    // interpolation, we offset the Halide integer grid by 0.5 so that these
    // coordinates line up on integer coordinate values.
    for (std::map<std::string,Expr>::iterator v = varyings.begin(); v != varyings.end(); ++v) {
        varyings[v->first] = 
            CastVariablesToFloatAndOffset(vec(loop0->name, loop1->name)).mutate(v->second);
    }
    
    // Establish and order for the attributes in each vertex
    std::map<std::string,int> attribute_order;
    
    // Add the attribute names to the mesh in the order that they appear in
    // each vertex
    result.attributes.push_back("__vertex_x");
    result.attributes.push_back("__vertex_y");
    
    attribute_order["__vertex_x"] = 0;
    attribute_order["__vertex_y"] = 1;
    
    int idx = 2;
    for (std::map<std::string,Expr>::iterator v = varyings.begin(); v != varyings.end(); ++v) {
        result.attributes.push_back(v->first);
        attribute_order[v->first] = idx++;
    }
    
    result.coords.resize(2 + varyings.size());
    
    // Construct a list of expressions giving to coordinate locations along
    // each dimension, starting with the minimum and maximum coordinates
    
    attribute_order[loop0->name] = 0;
    attribute_order[loop1->name] = 1;
    
    Expr loop0_max = Add::make(loop0->min,loop0->extent);
    Expr loop1_max = Add::make(loop1->min,loop1->extent);
    
    result.coords[0].push_back(loop0->min);
    result.coords[0].push_back(loop0_max);
    
    result.coords[1].push_back(loop1->min);
    result.coords[1].push_back(loop1_max);

    /*
    // TODO:(abstephensg) Need to integrate with specialize_branched_loops branch

    // Varying attribute expressions often contain piecewise linear
    // components, especially at the image border. These expressions often
    // depend on unknown parameters and cannot be evaluated during
    // compilation.  Instead we pass a list of expressions for the vertex
    // coordinates to the runtime, and it evaluates the expressions, sorts
    // their results, and produces the mesh.
    
    debug(2) << "Checking for piecewise linear expressions\n";
    
    for (std::map<std::string,Expr>::iterator v = varyings.begin(); v != varyings.end(); ++v) {
        
        // Determine the name of the variable without the .varying
        std::string varying_name = v->first;
        
        Expr value = v->second;
        
        debug(2) << "Original value\n" << value << "\n";
        
        std::vector<Expr> exprs = enumerate_branches(value);
        
        if (!exprs.size())
            continue;
        
        debug(2) << "Branch expressions\n";
        for (std::vector<Expr>::iterator e = exprs.begin(); e != exprs.end(); ++e) {
            debug(2) << *e << "\n";
        }
        
        debug(2) << "Solutions:\n";
        
        for (int j=0;j!=(int)exprs.size();++j) {
            Expr a = exprs[j];
            for (int i=j+1;i!=(int)exprs.size();++i) {
                Expr b = exprs[i];
                
                Expr eq = EQ::make(a, b);
                
                // Check to see if the equation can be solved in terms of
                // the varying
                for (auto var_name : { loop0->name, loop1->name }) {
                }
     
                if (contains_variable(eq, loop0->name)) {

                    Expr solution = solve_for_linear_variable_or_fail(eq, Var(loop0->name));

                    if (solution.defined()) {
                        debug(2) << "SOLVED: " << solution << "\n";
                        Expr rhs = solution.as<EQ>()->b;

                        int dim = attribute_order[loop0->name];
                        internal_assert(dim < 2) << "New coordinate must be in first or second dimension";
                        result.coords[dim].push_back(rhs);
                    } else {
                        internal_error << "GLSL Codegen: Did not solve: " << varying_name << " for: " << loop0->name << " expr: " << eq << "\n";
                    }
                }

                if (contains_variable(eq, loop1->name)) {

                    Expr solution = solve_for_linear_variable_or_fail(eq, Var(loop1->name));

                    if (solution.defined()) {
                        debug(2) << "SOLVED: " << solution << "\n";
                        Expr rhs = solution.as<EQ>()->b;

                        int dim = attribute_order[loop1->name];
                        internal_assert(dim < 2) << "New coordinate must be in first or second dimension";
                        result.coords[dim].push_back(rhs);
                    } else {
                        internal_error << "GLSL Codegen: Did not solve: " << varying_name << " for: " << loop1->name << " expr: " << eq << "\n";
                    }
                }


            }
        }
        debug(2) << "\n";
    }
    */


    // Create a list of expressions for each varying attribute that evaluates
    // it at each coordinate in the unsorted order of the coordinates found
    // above
    
    for (std::map<std::string,Expr>::iterator v = varyings.begin(); v != varyings.end(); ++v) {
        
        std::string varying_name = v->first;
        
        // Iterate over all of the coordinates for the variable in this
        // varying attribute expression
        
        // Determine the dimension (or interleaved channel) of the vertex
        // for this attribute
        int attrib_dim = attribute_order[varying_name];
        
        // The varying attribute expressions may be defined wrt both of the
        // loop variables. Produce pairs of let expressions to evaluate each
        // varying attribute expression at each pair of coordinates
        
        for (unsigned y = 0; y != result.coords[1].size(); ++y) {
            
            // Check if the varying expression contains the y dimension
            // variable and has the same type
            Expr cast_y = result.coords[1][y];
            
            ContainsVariable c(loop1->name);
            v->second.accept(&c);
            
            if (c.found && (c.result->type != cast_y.type())) {
                cast_y = Cast::make(c.result->type,cast_y);
            }

            for (unsigned x = 0; x != result.coords[0].size(); ++x) {
                
                // Check if the varying expression contains the y dimension
                // variable and has the same type
                Expr cast_x = result.coords[0][x];
                
                ContainsVariable c(loop0->name);
                v->second.accept(&c);
                
                if (c.found && (c.result->type != cast_x.type())) {
                    cast_x = Cast::make(c.result->type,cast_x);
                }

                Expr value = Let::make(loop1->name, cast_y, Let::make(loop0->name, cast_x, v->second));
                
                // Clean up the lets and other redundant terms
                value = simplify(value);

                // Add the expression for the varying attribute value to the vertex list
                result.coords[attrib_dim].push_back(value);
            }
        }
    }

    // Output coordinates for debugging
    debug(1) << "MESH COORD EXPRESSIONS:\n";
    
    for (unsigned a=0;a!=result.coords.size();++a) {
        std::string attrib_name = result.attributes[a];
        debug(1) << attrib_name << " (total: " << result.coords[a].size() << ")\n";

        for (std::vector<Expr>::const_iterator c = result.coords[a].begin(); c != result.coords[a].end(); ++c) {
            debug(1) << "    " << *c << "\n";
        }
    }
    
    return loop_stmt;
}

}
}
