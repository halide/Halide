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
    
// For debugging
class Unletify : public IRMutator {
public:
    using IRMutator::visit;
    virtual void visit(const Variable *op) {
        if (scope.contains(op->name)) {
            expr = mutate(scope.get(op->name));
        }
        else {
            expr = op;
        }
    }
    
    virtual void visit(const Let *op) {
        scope.push(op->name, op->value);
        expr = mutate(op->body);
        scope.pop(op->name);
    }
    
    virtual void visit(const LetStmt *op) {
        scope.push(op->name, op->value);
        stmt = mutate(op->body);
        scope.pop(op->name);
    }
    
    Unletify(Scope<Expr>& scope_) { scope.set_containing_scope(&scope_); }
    
    Scope<Expr> scope;
};
    
/** Find expressions that we can evaluate with interpolation hardware in the GPU
 *
 * This visitor keeps track of the "order" of the expression in terms of the
 * specified variables. The order value 0 means that the expression is contant;
 * order value 1 means that it is linear in terms of only one variable, check
 * the member found to determine which; order value 2 means non-linear, it
 * could be disqualified due to being quadratic, bilinear or the result of an
 * unknown function.
 */
class FindLinearExpressions : public IRMutator {
protected:
    using IRMutator::visit;
    
    Expr makeLinearLet(Expr e, const std::string& name = unique_name('a')) {
        
        // Perform some simplification: if the expression is a variable and its
        // name is in scope, then the visitor has already determined that it is
        // linear and will add a let for it higher in the IR tree. In this case,
        // skip replacing the expression with another .varying tagged let
        if (e.as<Variable>() && scope.contains(e.as<Variable>()->name)) {
            return e;
        }
        
        // Otherwise, replace the expression with a Let expression introducing
        // a variable tagged .varying. These tagged variables will be pulled
        // out of the fragment shader during a subsequent pass
        std::string var = name + ".varying";
        Expr let = Let::make(var, e, Variable::make(e.type(),var));

        linear_lets.push_back(let);
        return let;
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
                    new_args[2+i] = makeLinearLet(new_args[2+i]);
                }
            }
        }
        else if (op->name == Call::glsl_texture_store) {
            // Check if the value expression is linear wrt the loop variables
            internal_assert(loop_vars.size() > 0) << "No GPU loop variables found at texture load";
            
            // The value is the 5th argument to the intrinsic
            new_args[5] = mutate(new_args[5]);
            if (order == 1) {
                new_args[5] = makeLinearLet(new_args[5]);
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
        
        if (value_order == 1) {
            // Insert a let with a name tagged .varying, and point the existing
            // let name at this variable.
            std::string var = unique_name(op->name + ".varying");
            expr = Let::make(var, mutated_value, Let::make(op->name, Variable::make(mutated_value.type(), var), mutated_body));

            linear_lets.push_back(expr);
        }
        else {
            expr = Let::make(op->name,mutated_value,mutated_body);
        }
        
        scope.pop(op->name);
    }
    
    virtual void visit(const For *op) {
        // Check if the loop variable is a GPU variable thread variable
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            loop_vars.push_back(op->name);
        }
        
        Stmt mutated_body = mutate(op->body);
        
        // Check if this is the inner loop in the GPU for loop pair
        if (CodeGen_GPU_Dev::is_gpu_var(op->name) && loop_vars.size()==2) {
            
            // Examine the linearly varying attributes found and determine which
            // to move out of the fragment shader
            for (auto a : linear_lets) {
                
	        // TODO(abstephensg): Prioritize varying attributes in case we run out of slots
	        debug(1) << "VARYING ATTRIBUTE: " << a.as<Let>()->value.type() << " " << a.as<Let>()->name << " = " << a.as<Let>()->value << "\n\n";
                
                // ...
                
            }
        }
        
	if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
	    loop_vars.pop_back();
	}
        
        stmt = For::make(op->name, op->min, op->extent, op->for_type, mutated_body);
    }
    
    virtual void visit(const Variable *op) {
        if (op->name == name) {
            order = 1;
            found = true;
        }
        else if (std::find(loop_vars.begin(),loop_vars.end(),op->name) != loop_vars.end()) {
            order = 1;
        }
        else if (scope.contains(op->name)) {
            order = scope.get(op->name);
        }
        else {
            // If the variable is not found in scope, then we assume it is
            // constant in terms of the independent variables.
            order = 0;
        }
        expr = op;
    }
    
    template<typename T>
    void visitImm(T* op) {
        order = 0;
        expr = T::make(op->value);
    }
    
    virtual void visit(const IntImm *op)    { visitImm(op); }
    virtual void visit(const FloatImm *op)  { visitImm(op); }
    virtual void visit(const StringImm *op) { visitImm(op); }
    
    virtual void visit(const Cast *op) {
        
        Expr mutated_value = mutate(op->value);
        int value_order = order;
        
        // We can only interpolate float values, disqualify the expression if
        // this is a cast to a different type
        if (op->type.code != Type::Float) {
            order = 2;
        }
        
        if ((order > 1) && (value_order == 1)) {
            mutated_value = makeLinearLet(mutated_value);
        }
        
        expr = Cast::make(op->type, mutated_value);
    }
    
    // Add and subtract do not make the expression non-linear, if it is already
    // linear or constant
    template<typename T>
    void visitBinaryLinear(T* op) {
        Expr a = mutate(op->a);
        unsigned int order_a = order;
        Expr b = mutate(op->b);
        unsigned int order_b = order;
        
        order = std::max(order_a, order_b);
        
        // If the whole expression is greater than linear, check to see if
        // either argument is linear and if so, add it to a candidate list
        if ((order > 1) && (order_a == 1)) {
            a = makeLinearLet(a);
        }
        if ((order > 1) && (order_b == 1)) {
            b = makeLinearLet(b);
        }
        
        expr = T::make(a,b);
    }
    
    virtual void visit(const Add *op) { visitBinaryLinear(op); }
    virtual void visit(const Sub *op) { visitBinaryLinear(op); }
    
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
            a = makeLinearLet(a);
        }
        if ((order > 1) && (order_b == 1)) {
            b = makeLinearLet(b);
        }
        
        expr = Mul::make(a,b);
    }
    
    // Dividing is either multiplying by a constant, makes the result non-linear
    // (i.e. order -1)
    virtual void visit(const Div *op) {
        Expr a = mutate(op->a);
        unsigned int order_a = order;
        Expr b = mutate(op->b);
        unsigned int order_b = order;
        
        if (order_a && !order_b) {
            order = order_a;
        }
        else if (!order_a && order_b) {
            order = order_b;
        }
        else {
            std::vector<Expr> matches;
            if (expr_match(op->a,op->b,matches)) {
                order = 0;
            }
            else {
                order = order_a + order_b;
            }
        }
        
        expr = Div::make(a, b);
    }
    
    // For other binary operators, if either argument is non-constant, then the
    // whole expression is non-linear
    template<typename T>
    void visitBinary(T* op) {
        
        Expr a = mutate(op->a);
        unsigned int order_a = order;
        Expr b = mutate(op->b);
        unsigned int order_b = order;
        
        if (order_a || order_b) {
            order = 2;
        }
        
        if ((order > 1) && (order_a == 1)) {
            a = makeLinearLet(a);
        }
        if ((order > 1) && (order_b == 1)) {
            b = makeLinearLet(b);
        }
        
        expr = T::make(a,b);
    }
    
    virtual void visit(const Mod *op) { visitBinary(op); }
    
    // Break the expression into a piecewise function, if the expressions are
    // linear, we treat the piecewise behavior specially during codegen
    virtual void visit(const Min *op) { visitBinary(op); }
    virtual void visit(const Max *op) { visitBinary(op); }
    
    virtual void visit(const EQ *op) { visitBinary(op); }
    virtual void visit(const NE *op) { visitBinary(op); }
    virtual void visit(const LT *op) { visitBinary(op); }
    virtual void visit(const LE *op) { visitBinary(op); }
    virtual void visit(const GT *op) { visitBinary(op); }
    virtual void visit(const GE *op) { visitBinary(op); }
    virtual void visit(const And *op) { visitBinary(op); }
    virtual void visit(const Or *op) { visitBinary(op); }
    
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
            a = makeLinearLet(a);
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
            mutated_condition = makeLinearLet(mutated_condition);
        }
        if ((order > 1) && (true_value_order == 1)) {
            mutated_true_value = makeLinearLet(mutated_true_value);
        }
        if ((order > 1) && (false_value_order == 1)) {
            mutated_false_value = makeLinearLet(mutated_false_value);
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
    std::vector<Expr> linear_lets;
    
    Scope<int> scope;
    
    std::string name;
    unsigned int order;
    bool found;
};

Stmt find_linear_expressions(Stmt s) {
    
    return FindLinearExpressions().mutate(s);
}



/** Produce an expression containing a traversal through the branches in an IR 
 * tree based on a boolean vector of branch choices. For each binary branch
 * encountered, the boolean value indicates which expression is included in the
 * result.
 */
class TraverseBranches : public IRMutator
{
public:
    using IRMutator::visit;

    bool which() {
        if (depth == branch.size()) {
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
    void visitBinaryPiecewise(const T* op) {
        if (which() == 0) {
            push(op->a);
        }
        else {
            push(op->b);
        }
    }
    
    virtual void visit(const Min *op) { visitBinaryPiecewise(op); }
    virtual void visit(const Max *op) { visitBinaryPiecewise(op); }
    
    TraverseBranches(std::vector<bool> traversal_) : branch(traversal_), depth(0) { }
    
    std::vector<bool> branch;
    int depth;
};
    
/** This visitor traverses the IR tree to find vectors of branch choices to 
 supply to TraverseBranches.
 */
class EnumerateBranches : public IRVisitor
{
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
    void visitLeaf() {
        Expr e = TraverseBranches(traversal).mutate(root);
        result.push_back(e);
    }

    // Visit a binary piecewise operation like min/max
    template<typename T>
    void visitBinaryPiecewise(const T* op) {
        
        // Traverse the first branch expression
        traversal.push_back(0);
        count = 0;
        push(op->a);
        
        // Check if there are no branch expressions below this node, if so,
        // use the traversal vector to traverse the tree through this node and
        // produce a complete expression
        int count_a = count;
        if (!count) {
            visitLeaf();
        }
        
        // Traverse the second branch expression
        *traversal.rbegin() = 1;
        count = 0;
        push(op->b);
        int count_b = count;
        if (!count) {
            visitLeaf();
        }
        
        traversal.pop_back();
        
        // Keep track of the number of branch expressions found in this sub-tree
        count = std::max(count_a,count_b)+1;
    }
    
    virtual void visit(const Min *op) { visitBinaryPiecewise(op); }
    virtual void visit(const Max *op) { visitBinaryPiecewise(op); }
    
    std::vector<Expr> result;
    Expr root;
    
    std::vector<bool> traversal;
    int depth = 0;
    int count = 0;
};

/** This function returns a vector containing an expression for each possible
 branch within the specified expression. If the expression does not contain
 any branches, an empty vector is returned.
 */
std::vector<Expr> enumerate_branches(Expr root) {
    EnumerateBranches e;
    e.root = root;
    root.accept(&e);
    
    return e.result;
}

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
    
    internal_assert(c.found) << "Expression " << e << " does not contain variable " << name << "\n";
    
    return c.result->type;
}
        
class ExpressionMeshBuilder : public IRVisitor
{
public:
    ExpressionMeshBuilder(std::map<std::string,Expr>& varyings_) : varyings(varyings_) { }
    
    using IRVisitor::visit;
    
    virtual void visit(const Let *op) {
        if (ends_with(op->name,".varying")) {
            
            // Unletify the expression so that it can be moved outside of the
            // GPU For-loops and depend only on parameters
            varyings[op->name] = Unletify(scope).mutate(op->value);
        }
        
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

class RemoveVaryingAttributeLets : public IRMutator {
public:
    using IRMutator::visit;
    
    virtual void visit(const Let *op) {
        if (ends_with(op->name,".varying")) {
            
            // Skip the let statement, the variable name will become an argument
            // picked up by the host closure
            expr = mutate(op->body);
        }
        else {
            IRMutator::visit(op);
        }
    }
};

Stmt remove_varying_attributes(Stmt s)
{
    return RemoveVaryingAttributeLets().mutate(s);
}

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

void prune_varying_attributes(Stmt loop_stmt, std::map<std::string,Expr>& varying)
{
    FindVaryingAttributes find;
    loop_stmt.accept(&find);
    
    std::vector<std::string> remove_list;    
    for (auto i : varying) {
        const std::string& name = i.first;
        if (find.variables.find(name) == find.variables.end()) {
            debug(2) << "Removed varying attribute " << name << "\n";
            remove_list.push_back(name);
        }
    }
    
    for (auto name : remove_list) {
        varying.erase(name);
    }
}

Stmt setup_mesh(const For* op, ExpressionMesh& result, std::map<std::string,Expr>& varyings)
{
    // Construct a mesh of expressions to instantiate during runtime
    ExpressionMeshBuilder mesh_builder(varyings);
    op->accept(&mesh_builder);
    
    // Remove the varying attribute let expressions from the statement
    Stmt loop_stmt = remove_varying_attributes(op);
    
    // Perform the Let-simplification pass that was skipped during
    // Lowering
    loop_stmt = simplify(loop_stmt,true);
    
    // It is possible that linear expressions we tagged in higher-level
    // lets were removed by simplification if they were only used in
    // subsequent tagged linear expressions. Run a pass to check for
    // these and remove them from the varying attribute list
    prune_varying_attributes(loop_stmt,varyings);

    // Establish and order for the attributes in each vertex
    std::map<std::string,int> attribute_order;
    
    // Add the attribute names to the mesh in the order that they appear in
    // each vertex
    result.attributes.push_back("__vertex_x");
    result.attributes.push_back("__vertex_y");
    
    attribute_order["__vertex_x"] = 0;
    attribute_order["__vertex_y"] = 1;
    
    int idx = 2;
    for (auto v : varyings) {
        result.attributes.push_back(v.first);
        attribute_order[v.first] = idx++;
    }
    
    result.coords.resize(2 + varyings.size());
    
    // Construct a list of expressions giving to coordinate locations along
    // each dimension, starting with the minimum and maximum coordinates
    const For* loop1 = op;
    const For* loop0 = loop1->body.as<For>();
    
    internal_assert(loop1->body.as<For>()) << "Did not find pair of nested For loops";
    
    attribute_order[loop0->name] = 0;
    attribute_order[loop1->name] = 1;
    
    Expr loop0_max = Add::make(loop0->min,loop0->extent);
    Expr loop1_max = Add::make(loop1->min,loop1->extent);
    
    result.coords[0].push_back(loop0->min);
    result.coords[0].push_back(loop0_max);
    
    result.coords[1].push_back(loop1->min);
    result.coords[1].push_back(loop1_max);
    
    // Varying attribute expressions often contain piecewise linear
    // components, especially at the image border. These expressions often
    // depend on unknown parameters and cannot be evaluated during
    // compilation.  Instead we pass a list of expressions for the vertex
    // coordinates to the runtime, and it evaluates the expressions, sorts
    // their results, and produces the mesh.
    
    debug(2) << "Checking for piecewise linear expressions\n";
    
    for (auto v : varyings) {
        
        // Determine the name of the variable without the .varying
        std::string varying_name = v.first;
        
        Expr value = v.second;
        
        debug(2) << "Original value\n" << value << "\n";
        
        std::vector<Expr> exprs = enumerate_branches(value);
        
        if (!exprs.size())
            continue;
        
        debug(2) << "Branch expressions\n";
        for (auto e : exprs) {
            debug(2) << e << "\n";
        }
        
        debug(2) << "Solutions:\n";
        
        for (int j=0;j!=exprs.size();++j) {
            Expr a = exprs[j];
            for (int i=j+1;i!=exprs.size();++i) {
                Expr b = exprs[i];
                
                Expr eq = EQ::make(a, b);
                
                // Check to see if the equation can be solved in terms of
                // the varying
                for (auto var_name : { loop0->name, loop1->name }) {
                    
                    if (contains_variable(eq, var_name)) {

// TODO:(abstephensg) Need to integrate with specialize_branched_loops branch
/*
                        Expr solution = solve_for_linear_variable_or_fail(eq, Var(var_name));
                        
                        if (solution.defined()) {
                            debug(2) << "SOLVED: " << solution << "\n";
                            Expr rhs = solution.as<EQ>()->b;
                            
                            int dim = attribute_order[var_name];
                            internal_assert(dim < 2) << "New coordinate must be in first or second dimension";
                            result.coords[dim].push_back(rhs);
                        }
                        else {
*/
                            internal_error << "DID NOT SOLVE: " << varying_name << " FOR: " << var_name << " EXPR: " << eq << "\n";
/*
                        }
*/
                    }
                }
            }
        }
        debug(2) << "\n";
    }
    
    // Create a list of expressions for each varying attribute that evaluates
    // it at each coordinate in the unsorted order of the coordinates found
    // above
    
    for (auto v : varyings) {
        
        std::string varying_name = v.first;
        
        debug(1) << "\nVarying: " << varying_name << "\n";
        
        // Iterate over all of the coordinates for the variable in this
        // varying attribute expression
        
        // Determine the dimension (or interleaved channel) of the vertex
        // for this attribute
        int attrib_dim = attribute_order[varying_name];
        
        // The varying attribute expressions may be defined wrt both of the
        // loop variables. Produce pairs of let expressions to evaluate each
        // varying attribute expression at each pair of coordinates
        
        for (auto y : result.coords[1]) {
            
            // Check if the varying expression contains the y dimension
            // variable and has the same type
            Expr cast_y = y;
            
            ContainsVariable c(loop1->name);
            v.second.accept(&c);
            
            if (c.found && (c.result->type != y.type())) {
                cast_y = Cast::make(c.result->type,y);
            }

            for (auto x : result.coords[0]) {
                
                // Check if the varying expression contains the y dimension
                // variable and has the same type
                Expr cast_x = x;
                
                ContainsVariable c(loop0->name);
                v.second.accept(&c);
                
                if (c.found && (c.result->type != x.type())) {
                    cast_x = Cast::make(c.result->type,x);
                }

                Expr value = Let::make(loop1->name, cast_y, Let::make(loop0->name, cast_x, v.second));
                
                // Clean up the lets and other redundant terms
                value = simplify(value);

                // Add the expression for the varying attribute value to the vertex list
                result.coords[attrib_dim].push_back(value);
            }
        }
    }
    
    debug(1) << "MESH:\n";
    
    for (int a=0;a!=result.coords.size();++a) {
        std::string attrib_name = result.attributes[a];
        debug(1) << attrib_name << " (total: " << result.coords[a].size() << ")\n";

        for (auto c : result.coords[a]) {
            debug(1) << "    " << c << "\n";
        }
    }
    
    return loop_stmt;
}

}
}
