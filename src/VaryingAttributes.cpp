#include "VaryingAttributes.h"

#include "CodeGen_GPU_Dev.h"

#include "IRMutator.h"
#include "IRMatch.h"

#include "/Users/abstephens/Halide-kree/src/LinearSolve.h"

namespace Halide {
namespace Internal {
    
    
/** IsExprLinearInName determines if an expression is linear or constant in terms
 of a specific variable, e.g. the loop variable in a for loop over fragments
 or thread id on the GPU.
 
 This visitor keeps track of the "order" of the expression in terms of the
 specified variables. The order value 0 means that the expression is contant;
 order value 1 means that it is linear in terms of only one variable, check
 the member found to determine which; order value 2 means non-linear, it
 could be disqualified due to being quadratic, bilinear or the result of an
 unknown function.
 */
class IsExprLinearInName : public IRVisitor {
protected:
    using IRVisitor::visit;
    
    virtual void visit(const Variable *op) {
        if (std::find(names.begin(),names.end(),op->name) != names.end()) {
            order = 1;
            found = op->name;
        }
        else
            order = 0;
    }
    
    virtual void visit(const IntImm *)    { order = 0; }
    virtual void visit(const FloatImm *)  { order = 0; }
    virtual void visit(const StringImm *) { order = 0; }
    
    // Add and subtract do not make the expression non-linear, if it is already
    // linear or constant
    template<typename T>
    void visitBinaryLinear(T op) {
        op->a.accept(this);
        unsigned int order_a = order;
        op->b.accept(this);
        unsigned int order_b = order;
        
        order = std::max(order_a, order_b);
    }
    
    virtual void visit(const Add *op) { visitBinaryLinear(op); }
    virtual void visit(const Sub *op) { visitBinaryLinear(op); }
    
    // Multiplying increases the order of the expression, possibly making it
    // non-linear
    virtual void visit(const Mul *op) {
        op->a.accept(this);
        unsigned int order_a = order;
        op->b.accept(this);
        unsigned int order_b = order;
        
        order = order_a + order_b;
    }
    
    // Dividing is either multiplying by a constant, makes the result non-linear
    // (i.e. order -1)
    virtual void visit(const Div *op) {
        op->a.accept(this);
        unsigned int order_a = order;
        op->b.accept(this);
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
    }
    
    // For other binary operators, if either argument is non-constant, then the
    // whole expression is non-linear
    template<typename T>
    void visitBinary(T op) {
        op->a.accept(this);
        if (order) {
            order = 2;
            return;
        }
        op->b.accept(this);
        if (order) {
            order = 2;
            return;
        }
    }
    
    virtual void visit(const Mod *op) { visitBinary(op); }
    
    // Break the expression into a piecewise function, if the expressions are
    // linear, we treat the piecewise behavior specially during codegen
    virtual void visit(const Min *op) { visitBinaryLinear(op); }
    virtual void visit(const Max *op) { visitBinaryLinear(op); }
    
    virtual void visit(const EQ *op) { visitBinary(op); }
    virtual void visit(const NE *op) { visitBinary(op); }
    virtual void visit(const LT *op) { visitBinary(op); }
    virtual void visit(const LE *op) { visitBinary(op); }
    virtual void visit(const GT *op) { visitBinary(op); }
    virtual void visit(const GE *op) { visitBinary(op); }
    virtual void visit(const And *op) { visitBinary(op); }
    virtual void visit(const Or *op) { visitBinary(op); }
    
    virtual void visit(const Not *op) {
        op->a.accept(this);
        if (order) {
            order = 2;
            return;
        }
    }
    
    virtual void visit(const Select *op) {
        op->condition.accept(this);
        if (order) {
            order = 2;
            return;
        }
        op->true_value.accept(this);
        if (order) {
            order = 2;
            return;
        }
        op->false_value.accept(this);
        if (order) {
            order = 2;
            return;
        }
    }
    virtual void visit(const Call *op) {
        for (auto a : op->args) {
            a.accept(this);
            if (order) {
                order = 2;
                return;
            }
        }
    }
    virtual void visit(const IfThenElse *op) {
        op->condition.accept(this);
        if (order) {
            order = 2;
            return;
        }
        op->then_case.accept(this);
        if (order) {
            order = 2;
            return;
        }
        if (op->else_case.defined()) {
            op->else_case.accept(this);
            if (order) {
                order = 2;
                return;
            }
        }
    }
    
public:
    const std::vector<std::string> names;
    unsigned int order;
    std::string found;
    
    IsExprLinearInName(const std::vector<std::string>& names_) : names(names_), order(0) { };
};

/** Find expressions that we can evaluate with interpolation hardware in the GPU
 */
class FindLinearExpressions : public IRMutator {
protected:
    using IRMutator::visit;
    
    virtual void visit(const Call *op) {
        
        IRMutator::visit(op);
        
        // Check to see if this call is a load
        if (op->name == Call::glsl_texture_load) {
            // Check if the texture coordinate arguments are linear with
            // respect to the GPU loop variables
            internal_assert(loop_vars.size() > 0) << "No GPU loop variables found at texture load";
            
            bool replace = false;
            std::vector<Expr> new_args = op->args;
            
            // Iterate over the texture coordinate arguments
            for (int i=0;i!=2;++i) {
                
                int width = (op->args[2+i].as<Broadcast>()) ? op->args[2+i].as<Broadcast>()->width : 1;
                
                Expr arg = op->args[2+i];
                
                IsExprLinearInName query(loop_vars);
                arg.accept(&query);
                
                // Check if the expression is linear in terms of the variable
                if (query.order == 1) {
                    replace = true;
                    
                    // Move the argument expression to a let statement
                    std::string var = unique_name(query.found) + ".varying";
                    
                    // Replace the argument with the new variable
                    if (width > 1) {
                        Expr scalar_arg = arg.as<Broadcast>()->value;
                        linear_exprs[var] = scalar_arg;
                        new_args[2+i] = Broadcast::make(Variable::make(scalar_arg.type(), var), width);
                    }
                    else {
                        linear_exprs[var] = arg;
                        new_args[2+i] = Variable::make(arg.type(), var);
                    }
                }
            }
            
            // Check to see if we replaced any arguments
            if (replace) {
                expr = Call::make(op->type, op->name, new_args, op->call_type);
            }
        }
    }
    
    virtual void visit(const For *op) {
        // Check if the loop variable is a GPU variable thread variable
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            loop_vars.push_back(op->name);
        }
        
        IRMutator::visit(op);
        
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            // Check to see if we found any for this loop
            auto iter = linear_exprs.find(op->name + ".varying");
            if (iter == linear_exprs.end())
                return;
            
            // Examine the mutated pipeline
            const For* mutated = stmt.as<const For>();
            internal_assert(mutated);
            
            // Insert LetStmt immediately after after the For loop.
            std::string name = iter->first;
            Expr expr        = iter->second;
            
            // Create a new let for the substituted expression
            Stmt let = LetStmt::make(name, expr, mutated->body);
            
            stmt = For::make(op->name, op->min, op->extent, op->for_type, let);
        }
    }
    
public:
    std::map<std::string,Expr> linear_exprs;
    std::vector<std::string> loop_vars;
};

Stmt find_linear_expressions(Stmt s) {
    
    return FindLinearExpressions().mutate(s);
}

    

template<typename NodeType_>
class RegularGrid {
public:
    typedef NodeType_ NodeType;
    
    RegularGrid(const std::vector<int>& sizes_) : sizes(sizes_) {
        int total_size = 1;
        for (auto s : sizes_)
            total_size *= s;
        nodes.resize(total_size);
    }
    
    NodeType& operator()(const std::vector<int> indices) {
        int idx = indices[0];
        for (int d=1;d!=indices.size();++d)
            idx += (indices[d] * sizes[d-1]);
        return nodes[idx];
    }
    const NodeType& operator()(const std::vector<int> indices) const {
        int idx = indices[0];
        for (int d=1;d!=indices.size();++d)
            idx += (indices[d] * sizes[d-1]);
        return nodes[idx];
    }
    
    
private:
    std::vector<int> sizes;
    std::vector<NodeType> nodes;
};

/**
 * This class contains an irregular grid of nodes in Dim dimensions. The
 * coordinates along each dimension that divide the domain into nodes are
 * given by expressions and the NodeType template parameter is usually an
 * expression or statement, or list of expressions and statements.
 *
 * Since it may not be possible to evaluate the expressions during
 * compilation it is the responsibility of the calling code to maintain
 * order between the coordinates across each dimension.
 */
template<int Dim_, typename CoordType_, typename NodeType_>
class ExpressionGrid {
public:
    enum  { Dim = Dim_ };
    typedef CoordType_ CoordType;
    typedef NodeType_  NodeType;
    typedef RegularGrid<NodeType> RegularGridType;
    
    /**
     * Construct an expression grid initialized to cover the whole domain
     * with a single node covering the area between min_coords and
     * max_coords in each dimension
     */
    ExpressionGrid(const std::vector<CoordType> min_coords,const std::vector<CoordType> max_coords, NodeType node);
    
    /**
     * Split the node at the specified indices into min_expr and max_expr
     * along the specified dimension. Other nodes intersecting the split
     * coordinate will be split into two duplicates.
     */
    void split(const std::vector<int> indices, int dim, CoordType coord, NodeType min_expr, NodeType max_expr);
    
    int numVertices(int dim) const {
        return coords[dim].size();
    }
    
    NodeType& node(const std::vector<int> indices) {
        return nodes(indices);
    }
    
    CoordType& coord(int dim, int index) {
        return coords[dim][index];
    }
    
protected:
    std::vector<CoordType> coords[Dim];
    RegularGridType nodes;
};

    
class Vector : public std::vector<int> {
public:
    Vector(int dim, int value) : std::vector<int>(dim,value) { }
    Vector(const std::vector<int>& rhs) : std::vector<int>(rhs) { }
    
    Vector operator+(const Vector& rhs) {
        Vector r(size(),0);
        for (int i=0;i!=size();++i)
            r[i] = operator[](i) + rhs[i];
        return r;
    }
    
    int total() const {
        int r = 1;
        for (int s : *this)
            r *= s;
        return r;
    }
};
    
template<typename T>
void subgrid_copy(const Vector& region,
                  RegularGrid<T>& dst, Vector& dst_offset,
                  const RegularGrid<T>& src, Vector& src_offset)
{
    int Dim = region.size();
    
    Vector cursor(Dim,0);
    
    // Determine the total size of the region
    int total = region.total();
    
    for (int i=0;i!=total;++i) {
        
        // Add the source and destinaton offsets to the cursor
        Vector dst_coord = dst_offset + cursor;
        Vector src_coord = src_offset + cursor;
        
        dst(dst_coord) = src(src_coord);

        // Move the cursor to the next node
        cursor[0]++;
        for (int d=0;d!=Dim;++d) {
            if (cursor[d] == region[d]) {
                cursor[d] = 0;
                if ((d+1) != Dim)
                    cursor[d+1]++;
            }
        }
    }
}

template<int Dim_, typename CoordType_, typename NodeType_>
ExpressionGrid<Dim_,CoordType_,NodeType_>::ExpressionGrid(const std::vector<CoordType> min_coords,const std::vector<CoordType> max_coords, NodeType node) :
    nodes(Vector(Dim,1))
{
    // Construct a grid with two coordinates for the min and max in each
    // dimension
    auto min_expr = min_coords.begin();
    auto max_expr = max_coords.begin();
    
    for (int i=0;i!=Dim;++i) {
        coords[i].resize(2);
        coords[i][0] = *min_expr;
        coords[i][1] = *max_expr;
        min_expr++;
        max_expr++;
    }
    
    // The grid is initialized with a single node that covers the whole domain
    nodes({0,0,0}) = node;
}

template<int Dim_, typename CoordType_, typename NodeType_>
void ExpressionGrid<Dim_,CoordType_,NodeType_>::split(const std::vector<int> indices, int dim, CoordType coord, NodeType min_expr, NodeType max_expr)
{
    Vector index = indices;
    
    // The new slice of nodes is inserted at indices[dim]. Create a new grid
    // of nodes to accommodate the new slice
    
    // Determine the size of the new grid
    Vector sizes(Dim,0);
    for (int d=0;d!=Dim;++d) {
        sizes[d] = coords[d].size()-1;
    }
    
    // Increase the dimension we are inserting into by one
    sizes[dim]++;

    RegularGridType new_grid(sizes);

    // Insert the new coordinate
    coords[dim].insert(coords[dim].begin()+index[dim]+1,coord);

    Vector region = sizes;
    Vector zero(Dim,0);
    
    // Copy the subgrid ahead of the inserted slice
    region[dim] = index[dim];
    subgrid_copy(region,new_grid,zero,nodes,zero);
    
    // Duplicate the inserted slice
    region = sizes;
    region[dim] = 1;
    Vector source = zero;
    source[dim] = index[dim];
    subgrid_copy(region,new_grid,source,nodes,source);
    
    Vector duplicate = source;
    duplicate[dim]++;
    subgrid_copy(region,new_grid,duplicate,nodes,source);
    
    // Copy the subgrid after the inserted slice
    region = sizes;
    region[dim] = sizes[dim] - index[dim];
    duplicate[dim]++;
    source[dim]++;
    subgrid_copy(region,new_grid,duplicate,nodes,source);
    
    nodes = new_grid;
    
    // Update the expressions in the split node
    nodes(index) = min_expr;
    
    index[dim]++;
    nodes(index) = max_expr;
}

typedef ExpressionGrid<2, Expr, std::map<std::string,Expr>> GridType;


/** Produce an expression containing a traversal through the branches in an IR 
 tree based on a boolean vector of branch choices. For each binary branch 
 encountered, the boolean value indicates which expression is included in the 
 result.
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
    
    return c.result->type;
}
        
class ExpressionMeshBuilder : public IRVisitor
{
public:
    ExpressionMeshBuilder(ExpressionMesh& result_) : result(result_) { }
    
    using IRVisitor::visit;
    
    virtual void visit(const For *op) {
        
        bool outer_most = false;
        
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            outer_most = !loops.size();
            loops[op->name] = op;
        }
        
        // Visit the body of the loop to look for nested loops
        IRVisitor::visit(op);
        
        // Check to see if this is the top level loop
        if (!outer_most)
            return;
        
        // Otherwise create a mesh based on the pair of for-loops
        internal_assert(loops.size() == 2);

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
        result.attribute_dims.resize(2 + varyings.size());

        // Construct a list of expressions giving to coordinate locations along
        // each dimension, starting with the minimum and maximum coordinates
        auto loop0 = loops.begin();
        auto loop1 = loops.rbegin();
        
        attribute_order[loop0->first] = 0;
        attribute_order[loop1->first] = 1;
        
        Expr loop0_max = Add::make(loop0->second->min,loop0->second->extent);
        Expr loop1_max = Add::make(loop1->second->min,loop1->second->extent);
        Expr max_coords[] = { loop0_max, loop1_max };
        
        result.coords[0].push_back(loop0->second->min);
        result.coords[0].push_back(loop0_max);

        result.coords[1].push_back(loop1->second->min);
        result.coords[1].push_back(loop1_max);
        
        // Varying attribute expressions often contain piecewise linear
        // components, especially at the image border. These expressions often
        // depend on unknown parameters and cannot be evaluated during
        // compilation instead we pass a list of expressions for vertex
        // coordinates along each dimension to the runtime, it evaluates the
        // expressions, sorts them, and produces the mesh.
        
        for (auto v : varyings) {
            
            // Determine the name of the variable without the .varying
            std::string varying_name = v.first;
            std::string var_name = varying_name.substr(0,varying_name.rfind('.'));

            Expr value = v.second;

            debug(2) << "Original value\n" << value << "\n";
            
            std::vector<Expr> exprs = enumerate_branches(value);

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
                    if (contains_variable(eq, var_name)) {
                        Expr solution = solve_for_linear_variable_or_fail(eq, Var(var_name));

                        if (solution.defined()) {
                            debug(2) << "SOLVED: " << solution << "\n";
                            Expr rhs = solution.as<EQ>()->b;
                            
                            int dim = attribute_order[var_name];
                            result.coords[dim].push_back(rhs);
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
            std::string var_name = varying_name.substr(0,varying_name.rfind('.'));
            
            // Iterate over all of the coordinates for the variable in this
            // varying attribute expression
            
            // Determine the dimension (or interleaved channel) of the vertex
            // for this attribute
            int attrib_dim = attribute_order[varying_name];
            
            // Record the spatial dimension that the attribute is defined w.r.t.
            int dim = attribute_order[var_name];
            result.attribute_dims[attrib_dim] = dim;
            
            for (auto c : result.coords[dim]) {
                
                Type let_var_type = type_of_variable(v.second, var_name);
                
                debug(2) << "Coord Type: " << c.type() << "\n";
                
                debug(2) << "Let Type: " << let_var_type << "\n";

                if (c.type() != let_var_type) {
                    c = Cast::make(let_var_type,c);
                }
                
                Expr value = Let::make(var_name, c, v.second);

                debug(2) << "Expr: " << value << "\n";
                
                // Add the expression for the varying attribute value to the vertex list
                result.coords[attrib_dim].push_back(value);
            }
        }
    }
    
    virtual void visit(const LetStmt *op) {
        if (loops.size() && ends_with(op->name,"varying")) {
            varyings[op->name] = op->value;
        }
        IRVisitor::visit(op);
    }

private:
    std::map<std::string,const For*> loops;
    std::map<std::string,Expr> varyings;

    ExpressionMesh& result;
};
    
    
void compute_mesh(const For* op, ExpressionMesh& result)
{
    // Construct a mesh of expressions to instantiate during runtime
    ExpressionMeshBuilder mesh_builder(result);
    op->accept(&mesh_builder);
    
    debug(2) << "MESH:\n";
    
    for (int a=0;a!=result.coords.size();++a) {
        std::string attrib_name = result.attributes[a];
        debug(2) << attrib_name << " (total: " << result.coords[a].size() << ")\n";

        for (auto c : result.coords[a]) {
            debug(2) << "    " << c << "\n";
        }
    }
    
}

}
}
