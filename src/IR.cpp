#include "IR.h"
#include "IRPrinter.h"
#include "IRVisitor.h"

namespace Halide {
namespace Internal {

Expr Cast::make(Type t, const Expr &v) {
    internal_assert(v.defined()) << "Cast of undefined\n";
    internal_assert(t.lanes() == v.type().lanes()) << "Cast may not change vector widths\n";

    Cast *node = new Cast;
    node->type = t;
    node->value = v;
    return node;
}

Expr Add::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "Add of undefined\n";
    internal_assert(b.defined()) << "Add of undefined\n";
    internal_assert(a.type() == b.type()) << "Add of mismatched types\n";

    Add *node = new Add;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Sub::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "Sub of undefined\n";
    internal_assert(b.defined()) << "Sub of undefined\n";
    internal_assert(a.type() == b.type()) << "Sub of mismatched types\n";

    Sub *node = new Sub;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Mul::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "Mul of undefined\n";
    internal_assert(b.defined()) << "Mul of undefined\n";
    internal_assert(a.type() == b.type()) << "Mul of mismatched types\n";

    Mul *node = new Mul;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Div::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "Div of undefined\n";
    internal_assert(b.defined()) << "Div of undefined\n";
    internal_assert(a.type() == b.type()) << "Div of mismatched types\n";

    Div *node = new Div;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Mod::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "Mod of undefined\n";
    internal_assert(b.defined()) << "Mod of undefined\n";
    internal_assert(a.type() == b.type()) << "Mod of mismatched types\n";

    Mod *node = new Mod;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Min::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "Min of undefined\n";
    internal_assert(b.defined()) << "Min of undefined\n";
    internal_assert(a.type() == b.type()) << "Min of mismatched types\n";

    Min *node = new Min;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Max::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "Max of undefined\n";
    internal_assert(b.defined()) << "Max of undefined\n";
    internal_assert(a.type() == b.type()) << "Max of mismatched types\n";

    Max *node = new Max;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr EQ::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "EQ of undefined\n";
    internal_assert(b.defined()) << "EQ of undefined\n";
    internal_assert(a.type() == b.type()) << "EQ of mismatched types\n";

    EQ *node = new EQ;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}

Expr NE::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "NE of undefined\n";
    internal_assert(b.defined()) << "NE of undefined\n";
    internal_assert(a.type() == b.type()) << "NE of mismatched types\n";

    NE *node = new NE;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}

Expr LT::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "LT of undefined\n";
    internal_assert(b.defined()) << "LT of undefined\n";
    internal_assert(a.type() == b.type()) << "LT of mismatched types\n";

    LT *node = new LT;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}


Expr LE::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "LE of undefined\n";
    internal_assert(b.defined()) << "LE of undefined\n";
    internal_assert(a.type() == b.type()) << "LE of mismatched types\n";

    LE *node = new LE;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}

Expr GT::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "GT of undefined\n";
    internal_assert(b.defined()) << "GT of undefined\n";
    internal_assert(a.type() == b.type()) << "GT of mismatched types\n";

    GT *node = new GT;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}


Expr GE::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "GE of undefined\n";
    internal_assert(b.defined()) << "GE of undefined\n";
    internal_assert(a.type() == b.type()) << "GE of mismatched types\n";

    GE *node = new GE;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}

Expr And::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "And of undefined\n";
    internal_assert(b.defined()) << "And of undefined\n";
    internal_assert(a.type().is_bool()) << "lhs of And is not a bool\n";
    internal_assert(b.type().is_bool()) << "rhs of And is not a bool\n";
    internal_assert(a.type() == b.type()) << "And of mismatched types\n";

    And *node = new And;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}

Expr Or::make(const Expr &a, const Expr &b) {
    internal_assert(a.defined()) << "Or of undefined\n";
    internal_assert(b.defined()) << "Or of undefined\n";
    internal_assert(a.type().is_bool()) << "lhs of Or is not a bool\n";
    internal_assert(b.type().is_bool()) << "rhs of Or is not a bool\n";
    internal_assert(a.type() == b.type()) << "Or of mismatched types\n";

    Or *node = new Or;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}

Expr Not::make(const Expr &a) {
    internal_assert(a.defined()) << "Not of undefined\n";
    internal_assert(a.type().is_bool()) << "argument of Not is not a bool\n";

    Not *node = new Not;
    node->type = Bool(a.type().lanes());
    node->a = a;
    return node;
}

Expr Select::make(const Expr &condition, const Expr &true_value, const Expr &false_value) {
    internal_assert(condition.defined()) << "Select of undefined\n";
    internal_assert(true_value.defined()) << "Select of undefined\n";
    internal_assert(false_value.defined()) << "Select of undefined\n";
    internal_assert(condition.type().is_bool()) << "First argument to Select is not a bool: " << condition.type() << "\n";
    internal_assert(false_value.type() == true_value.type()) << "Select of mismatched types\n";
    internal_assert(condition.type().is_scalar() ||
                    condition.type().lanes() == true_value.type().lanes())
        << "In Select, vector lanes of condition must either be 1, or equal to vector lanes of arguments\n";

    Select *node = new Select;
    node->type = true_value.type();
    node->condition = condition;
    node->true_value = true_value;
    node->false_value = false_value;
    return node;
}

Expr Load::make(Type type, const std::string &name, const Expr &index, Buffer<> image, Parameter param, const Expr &predicate) {
    internal_assert(predicate.defined()) << "Load with undefined predicate\n";
    internal_assert(index.defined()) << "Load of undefined\n";
    internal_assert(type.lanes() == index.type().lanes()) << "Vector lanes of Load must match vector lanes of index\n";
    internal_assert(type.lanes() == predicate.type().lanes())
        << "Vector lanes of Load must match vector lanes of predicate\n";

    Load *node = new Load;
    node->type = type;
    node->name = name;
    node->predicate = predicate;
    node->index = index;
    node->image = image;
    node->param = param;
    return node;
}

Expr Ramp::make(const Expr &base, const Expr &stride, int lanes) {
    internal_assert(base.defined()) << "Ramp of undefined\n";
    internal_assert(stride.defined()) << "Ramp of undefined\n";
    internal_assert(base.type().is_scalar()) << "Ramp with vector base\n";
    internal_assert(stride.type().is_scalar()) << "Ramp with vector stride\n";
    internal_assert(lanes > 1) << "Ramp of lanes <= 1\n";
    internal_assert(stride.type() == base.type()) << "Ramp of mismatched types\n";

    Ramp *node = new Ramp;
    node->type = base.type().with_lanes(lanes);
    node->base = base;
    node->stride = stride;
    node->lanes = lanes;
    return node;
}

Expr Broadcast::make(const Expr &value, int lanes) {
    internal_assert(value.defined()) << "Broadcast of undefined\n";
    internal_assert(value.type().is_scalar()) << "Broadcast of vector\n";
    internal_assert(lanes != 1) << "Broadcast of lanes 1\n";

    Broadcast *node = new Broadcast;
    node->type = value.type().with_lanes(lanes);
    node->value = value;
    node->lanes = lanes;
    return node;
}

Expr Let::make(const std::string &name, const Expr &value, const Expr &body) {
    internal_assert(value.defined()) << "Let of undefined\n";
    internal_assert(body.defined()) << "Let of undefined\n";

    Let *node = new Let;
    node->type = body.type();
    node->name = name;
    node->value = value;
    node->body = body;
    return node;
}

Stmt LetStmt::make(const std::string &name, const Expr &value, const Stmt &body) {
    internal_assert(value.defined()) << "Let of undefined\n";
    internal_assert(body.defined()) << "Let of undefined\n";

    LetStmt *node = new LetStmt;
    node->name = name;
    node->value = value;
    node->body = body;
    return node;
}

Stmt AssertStmt::make(const Expr &condition, const Expr &message) {
    internal_assert(condition.defined()) << "AssertStmt of undefined\n";
    internal_assert(message.type() == Int(32)) << "AssertStmt message must be an int:" << message << "\n";

    AssertStmt *node = new AssertStmt;
    node->condition = condition;
    node->message = message;
    return node;
}

Stmt ProducerConsumer::make(const std::string &name, bool is_producer, const Stmt &body) {
    internal_assert(body.defined()) << "ProducerConsumer of undefined\n";

    ProducerConsumer *node = new ProducerConsumer;
    node->name = name;
    node->is_producer = is_producer;
    node->body = body;
    return node;
}

Stmt ProducerConsumer::make_produce(const std::string &name, const Stmt &body) {
    return ProducerConsumer::make(name, true, body);
}

Stmt ProducerConsumer::make_consume(const std::string &name, const Stmt &body) {
    return ProducerConsumer::make(name, false, body);
}

Stmt For::make(const std::string &name, const Expr &min, const Expr &extent, ForType for_type, DeviceAPI device_api, const Stmt &body) {
    internal_assert(min.defined()) << "For of undefined\n";
    internal_assert(extent.defined()) << "For of undefined\n";
    internal_assert(min.type().is_scalar()) << "For with vector min\n";
    internal_assert(extent.type().is_scalar()) << "For with vector extent\n";
    internal_assert(body.defined()) << "For of undefined\n";

    For *node = new For;
    node->name = name;
    node->min = min;
    node->extent = extent;
    node->for_type = for_type;
    node->device_api = device_api;
    node->body = body;
    return node;
}

Stmt Store::make(const std::string &name, const Expr &value, const Expr &index, Parameter param, const Expr &predicate) {
    internal_assert(predicate.defined()) << "Store with undefined predicate\n";
    internal_assert(value.defined()) << "Store of undefined\n";
    internal_assert(index.defined()) << "Store of undefined\n";
    internal_assert(value.type().lanes() == index.type().lanes()) << "Vector lanes of Store must match vector lanes of index\n";
    internal_assert(value.type().lanes() == predicate.type().lanes())
        << "Vector lanes of Store must match vector lanes of predicate\n";

    Store *node = new Store;
    node->name = name;
    node->predicate = predicate;
    node->value = value;
    node->index = index;
    node->param = param;
    return node;
}

Stmt Provide::make(const std::string &name, const std::vector<Expr> &values, const std::vector<Expr> &args) {
    internal_assert(!values.empty()) << "Provide of no values\n";
    for (size_t i = 0; i < values.size(); i++) {
        internal_assert(values[i].defined()) << "Provide of undefined value\n";
    }
    for (size_t i = 0; i < args.size(); i++) {
        internal_assert(args[i].defined()) << "Provide to undefined location\n";
    }

    Provide *node = new Provide;
    node->name = name;
    node->values = values;
    node->args = args;
    return node;
}

Stmt Allocate::make(const std::string &name, Type type, const std::vector<Expr> &extents,
                    const Expr &condition, const Stmt &body,
                    const Expr &new_expr, const std::string &free_function) {
    for (size_t i = 0; i < extents.size(); i++) {
        internal_assert(extents[i].defined()) << "Allocate of undefined extent\n";
        internal_assert(extents[i].type().is_scalar() == 1) << "Allocate of vector extent\n";
    }
    internal_assert(body.defined()) << "Allocate of undefined\n";
    internal_assert(condition.defined()) << "Allocate with undefined condition\n";
    internal_assert(condition.type().is_bool()) << "Allocate condition is not boolean\n";

    Allocate *node = new Allocate;
    node->name = name;
    node->type = type;
    node->extents = extents;
    node->new_expr = new_expr;
    node->free_function = free_function;
    node->condition = condition;
    node->body = body;
    return node;
}

int32_t Allocate::constant_allocation_size(const std::vector<Expr> &extents, const std::string &name) {
    int64_t result = 1;

    for (size_t i = 0; i < extents.size(); i++) {
        if (const IntImm *int_size = extents[i].as<IntImm>()) {
            // Check if the individual dimension is > 2^31 - 1. Not
            // currently necessary because it's an int32_t, which is
            // always smaller than 2^31 - 1. If we ever upgrade the
            // type of IntImm but not the maximum allocation size, we
            // should re-enable this.
            /*
            if ((int64_t)int_size->value > (((int64_t)(1)<<31) - 1)) {
                user_error
                    << "Dimension " << i << " for allocation " << name << " has size " <<
                    int_size->value << " which is greater than 2^31 - 1.";
            }
            */
            result *= int_size->value;
            if (result > (static_cast<int64_t>(1)<<31) - 1) {
                user_error
                    << "Total size for allocation " << name
                    << " is constant but exceeds 2^31 - 1.\n";
            }
        } else {
            return 0;
        }
    }

    return static_cast<int32_t>(result);
}

int32_t Allocate::constant_allocation_size() const {
    return Allocate::constant_allocation_size(extents, name);
}

Stmt Free::make(const std::string &name) {
    Free *node = new Free;
    node->name = name;
    return node;
}

Stmt Realize::make(const std::string &name, const std::vector<Type> &types, const Region &bounds, const Expr &condition, const Stmt &body) {
    for (size_t i = 0; i < bounds.size(); i++) {
        internal_assert(bounds[i].min.defined()) << "Realize of undefined\n";
        internal_assert(bounds[i].extent.defined()) << "Realize of undefined\n";
        internal_assert(bounds[i].min.type().is_scalar()) << "Realize of vector size\n";
        internal_assert(bounds[i].extent.type().is_scalar()) << "Realize of vector size\n";
    }
    internal_assert(body.defined()) << "Realize of undefined\n";
    internal_assert(!types.empty()) << "Realize has empty type\n";
    internal_assert(condition.defined()) << "Realize with undefined condition\n";
    internal_assert(condition.type().is_bool()) << "Realize condition is not boolean\n";

    Realize *node = new Realize;
    node->name = name;
    node->types = types;
    node->bounds = bounds;
    node->condition = condition;
    node->body = body;
    return node;
}

Stmt Block::make(const Stmt &first, const Stmt &rest) {
    internal_assert(first.defined()) << "Block of undefined\n";
    internal_assert(rest.defined()) << "Block of undefined\n";

    Block *node = new Block;

    if (const Block *b = first.as<Block>()) {
        // Use a canonical block nesting order
        node->first = b->first;
        node->rest  = Block::make(b->rest, rest);
    } else {
        node->first = first;
        node->rest = rest;
    }

    return node;
}

Stmt Block::make(const std::vector<Stmt> &stmts) {
    if (stmts.empty()) {
        return Stmt();
    }
    Stmt result = stmts.back();
    for (size_t i = stmts.size()-1; i > 0; i--) {
        result = Block::make(stmts[i-1], result);
    }
    return result;
}

Stmt IfThenElse::make(const Expr &condition, const Stmt &then_case, const Stmt &else_case) {
    internal_assert(condition.defined() && then_case.defined()) << "IfThenElse of undefined\n";
    // else_case may be null.

    IfThenElse *node = new IfThenElse;
    node->condition = condition;
    node->then_case = then_case;
    node->else_case = else_case;
    return node;
}

Stmt Evaluate::make(const Expr &v) {
    internal_assert(v.defined()) << "Evaluate of undefined\n";

    Evaluate *node = new Evaluate;
    node->value = v;
    return node;
}

Expr Call::make(Function func, const std::vector<Expr> &args, int idx) {
    internal_assert(idx >= 0 &&
                    idx < func.outputs())
        << "Value index out of range in call to halide function\n";
    internal_assert(func.has_pure_definition() || func.has_extern_definition())
        << "Call to undefined halide function\n";
    return make(func.output_types()[(size_t)idx], func.name(), args, Halide, func.get_contents(), idx, Buffer<>(), Parameter());
}

Expr Call::make(Type type, const std::string &name, const std::vector<Expr> &args, CallType call_type,
                IntrusivePtr<FunctionContents> func, int value_index,
                Buffer<> image, Parameter param) {
    for (size_t i = 0; i < args.size(); i++) {
        internal_assert(args[i].defined()) << "Call of undefined\n";
    }
    if (call_type == Halide) {
        for (size_t i = 0; i < args.size(); i++) {
            internal_assert(args[i].type() == Int(32))
            << "Args to call to halide function must be type Int(32)\n";
        }
    } else if (call_type == Image) {
        internal_assert((param.defined() || image.defined()))
            << "Call node to undefined image\n";
        for (size_t i = 0; i < args.size(); i++) {
            internal_assert(args[i].type() == Int(32))
                << "Args to load from image must be type Int(32)\n";
        }
    }

    Call *node = new Call;
    node->type = type;
    node->name = name;
    node->args = args;
    node->call_type = call_type;
    node->func = func;
    node->value_index = value_index;
    node->image = image;
    node->param = param;
    return node;
}

Expr Variable::make(Type type, const std::string &name, Buffer<> image, Parameter param, ReductionDomain reduction_domain) {
    internal_assert(!name.empty());
    Variable *node = new Variable;
    node->type = type;
    node->name = name;
    node->image = image;
    node->param = param;
    node->reduction_domain = reduction_domain;
    return node;
}

Expr Shuffle::make(const std::vector<Expr> &vectors,
                   const std::vector<int> &indices) {
    internal_assert(!vectors.empty()) << "Shuffle of zero vectors.\n";
    internal_assert(!indices.empty()) << "Shufle with zero indices.\n";
    Type element_ty = vectors.front().type().element_of();
    int input_lanes = 0;
    for (Expr i : vectors) {
        internal_assert(i.type().element_of() == element_ty) << "Shuffle of vectors of mismatched types.\n";
        input_lanes += i.type().lanes();
    }
    for (int i : indices) {
        internal_assert(0 <= i && i < input_lanes) << "Shuffle vector index out of range: " << i << "\n";
    }

    Shuffle *node = new Shuffle;
    node->type = element_ty.with_lanes((int)indices.size());
    node->vectors = vectors;
    node->indices = indices;
    return node;
}

Expr Shuffle::make_interleave(const std::vector<Expr> &vectors) {
    internal_assert(!vectors.empty()) << "Interleave of zero vectors.\n";

    if (vectors.size() == 1) {
        return vectors.front();
    }

    int lanes = vectors.front().type().lanes();

    for (Expr i : vectors) {
        internal_assert(i.type().lanes() == lanes)
            << "Interleave of vectors with different sizes.\n";
    }

    std::vector<int> indices;
    for (int i = 0; i < lanes; i++) {
        for (int j = 0; j < (int)vectors.size(); j++) {
            indices.push_back(j * lanes + i);
        }
    }

    return make(vectors, indices);
}

Expr Shuffle::make_concat(const std::vector<Expr> &vectors) {
    internal_assert(!vectors.empty()) << "Concat of zero vectors.\n";

    if (vectors.size() == 1) {
        return vectors.front();
    }

    std::vector<int> indices;
    int lane = 0;
    for (int i = 0; i < (int)vectors.size(); i++) {
        for (int j = 0; j < vectors[i].type().lanes(); j++) {
            indices.push_back(lane++);
        }
    }

    return make(vectors, indices);
}

Expr Shuffle::make_slice(const Expr &vector, int begin, int stride, int size) {
    if (begin == 0 && size == vector.type().lanes() && stride == 1) {
        return vector;
    }

    std::vector<int> indices;
    for (int i = 0; i < size; i++) {
        indices.push_back(begin + i * stride);
    }

    return make({vector}, indices);
}

Expr Shuffle::make_extract_element(const Expr &vector, int i) {
    return make_slice(vector, i, 1, 1);
}

bool Shuffle::is_interleave() const {
    int lanes = vectors.front().type().lanes();

    // Don't consider concat of scalars as an interleave.
    if (lanes == 1) {
        return false;
    }

    for (Expr i : vectors) {
        if (i.type().lanes() != lanes) {
            return false;
        }
    }

    // Require that we are a complete interleaving.
    if (lanes * vectors.size() != indices.size()) {
        return false;
    }

    for (int i = 0; i < (int)vectors.size(); i++) {
        for (int j = 0; j < lanes; j++) {
            if (indices[j * (int)vectors.size() + i] != i * lanes + j) {
                return false;
            }
        }
    }

    return true;
}

namespace {

// Helper function to determine if a sequence of indices is a
// contiguous ramp.
bool is_ramp(const std::vector<int> &indices, int stride = 1) {
    for (size_t i = 0; i + 1 < indices.size(); i++) {
        if (indices[i + 1] != indices[i] + stride) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool Shuffle::is_concat() const {
    size_t input_lanes = 0;
    for (Expr i : vectors ) {
        input_lanes += i.type().lanes();
    }

    // A concat is a ramp where the output has the same number of
    // lanes as the input.
    return indices.size() == input_lanes && is_ramp(indices);
}

bool Shuffle::is_slice() const {
    size_t input_lanes = 0;
    for (Expr i : vectors ) {
        input_lanes += i.type().lanes();
    }

    // A slice is a ramp where the output does not contain all of the
    // lanes of the input.
    return indices.size() < input_lanes && is_ramp(indices, slice_stride());
}

bool Shuffle::is_extract_element() const {
    return indices.size() == 1;
}


template<> void ExprNode<IntImm>::accept(IRVisitor *v) const { v->visit((const IntImm *)this); }
template<> void ExprNode<UIntImm>::accept(IRVisitor *v) const { v->visit((const UIntImm *)this); }
template<> void ExprNode<FloatImm>::accept(IRVisitor *v) const { v->visit((const FloatImm *)this); }
template<> void ExprNode<StringImm>::accept(IRVisitor *v) const { v->visit((const StringImm *)this); }
template<> void ExprNode<Cast>::accept(IRVisitor *v) const { v->visit((const Cast *)this); }
template<> void ExprNode<Variable>::accept(IRVisitor *v) const { v->visit((const Variable *)this); }
template<> void ExprNode<Add>::accept(IRVisitor *v) const { v->visit((const Add *)this); }
template<> void ExprNode<Sub>::accept(IRVisitor *v) const { v->visit((const Sub *)this); }
template<> void ExprNode<Mul>::accept(IRVisitor *v) const { v->visit((const Mul *)this); }
template<> void ExprNode<Div>::accept(IRVisitor *v) const { v->visit((const Div *)this); }
template<> void ExprNode<Mod>::accept(IRVisitor *v) const { v->visit((const Mod *)this); }
template<> void ExprNode<Min>::accept(IRVisitor *v) const { v->visit((const Min *)this); }
template<> void ExprNode<Max>::accept(IRVisitor *v) const { v->visit((const Max *)this); }
template<> void ExprNode<EQ>::accept(IRVisitor *v) const { v->visit((const EQ *)this); }
template<> void ExprNode<NE>::accept(IRVisitor *v) const { v->visit((const NE *)this); }
template<> void ExprNode<LT>::accept(IRVisitor *v) const { v->visit((const LT *)this); }
template<> void ExprNode<LE>::accept(IRVisitor *v) const { v->visit((const LE *)this); }
template<> void ExprNode<GT>::accept(IRVisitor *v) const { v->visit((const GT *)this); }
template<> void ExprNode<GE>::accept(IRVisitor *v) const { v->visit((const GE *)this); }
template<> void ExprNode<And>::accept(IRVisitor *v) const { v->visit((const And *)this); }
template<> void ExprNode<Or>::accept(IRVisitor *v) const { v->visit((const Or *)this); }
template<> void ExprNode<Not>::accept(IRVisitor *v) const { v->visit((const Not *)this); }
template<> void ExprNode<Select>::accept(IRVisitor *v) const { v->visit((const Select *)this); }
template<> void ExprNode<Load>::accept(IRVisitor *v) const { v->visit((const Load *)this); }
template<> void ExprNode<Ramp>::accept(IRVisitor *v) const { v->visit((const Ramp *)this); }
template<> void ExprNode<Broadcast>::accept(IRVisitor *v) const { v->visit((const Broadcast *)this); }
template<> void ExprNode<Call>::accept(IRVisitor *v) const { v->visit((const Call *)this); }
template<> void ExprNode<Shuffle>::accept(IRVisitor *v) const { v->visit((const Shuffle *)this); }
template<> void ExprNode<Let>::accept(IRVisitor *v) const { v->visit((const Let *)this); }
template<> void StmtNode<LetStmt>::accept(IRVisitor *v) const { v->visit((const LetStmt *)this); }
template<> void StmtNode<AssertStmt>::accept(IRVisitor *v) const { v->visit((const AssertStmt *)this); }
template<> void StmtNode<ProducerConsumer>::accept(IRVisitor *v) const { v->visit((const ProducerConsumer *)this); }
template<> void StmtNode<For>::accept(IRVisitor *v) const { v->visit((const For *)this); }
template<> void StmtNode<Store>::accept(IRVisitor *v) const { v->visit((const Store *)this); }
template<> void StmtNode<Provide>::accept(IRVisitor *v) const { v->visit((const Provide *)this); }
template<> void StmtNode<Allocate>::accept(IRVisitor *v) const { v->visit((const Allocate *)this); }
template<> void StmtNode<Free>::accept(IRVisitor *v) const { v->visit((const Free *)this); }
template<> void StmtNode<Realize>::accept(IRVisitor *v) const { v->visit((const Realize *)this); }
template<> void StmtNode<Block>::accept(IRVisitor *v) const { v->visit((const Block *)this); }
template<> void StmtNode<IfThenElse>::accept(IRVisitor *v) const { v->visit((const IfThenElse *)this); }
template<> void StmtNode<Evaluate>::accept(IRVisitor *v) const { v->visit((const Evaluate *)this); }

Call::ConstString Call::debug_to_file = "debug_to_file";
Call::ConstString Call::reinterpret = "reinterpret";
Call::ConstString Call::bitwise_and = "bitwise_and";
Call::ConstString Call::bitwise_not = "bitwise_not";
Call::ConstString Call::bitwise_xor = "bitwise_xor";
Call::ConstString Call::bitwise_or = "bitwise_or";
Call::ConstString Call::shift_left = "shift_left";
Call::ConstString Call::shift_right = "shift_right";
Call::ConstString Call::abs = "abs";
Call::ConstString Call::absd = "absd";
Call::ConstString Call::lerp = "lerp";
Call::ConstString Call::random = "random";
Call::ConstString Call::popcount = "popcount";
Call::ConstString Call::count_leading_zeros = "count_leading_zeros";
Call::ConstString Call::count_trailing_zeros = "count_trailing_zeros";
Call::ConstString Call::undef = "undef";
Call::ConstString Call::address_of = "address_of";
Call::ConstString Call::return_second = "return_second";
Call::ConstString Call::if_then_else = "if_then_else";
Call::ConstString Call::glsl_texture_load = "glsl_texture_load";
Call::ConstString Call::glsl_texture_store = "glsl_texture_store";
Call::ConstString Call::glsl_varying = "glsl_varying";
Call::ConstString Call::image_load = "image_load";
Call::ConstString Call::image_store = "image_store";
Call::ConstString Call::make_struct = "make_struct";
Call::ConstString Call::stringify = "stringify";
Call::ConstString Call::memoize_expr = "memoize_expr";
Call::ConstString Call::alloca = "alloca";
Call::ConstString Call::copy_memory = "copy_memory";
Call::ConstString Call::likely = "likely";
Call::ConstString Call::likely_if_innermost = "likely_if_innermost";
Call::ConstString Call::register_destructor = "register_destructor";
Call::ConstString Call::div_round_to_zero = "div_round_to_zero";
Call::ConstString Call::mod_round_to_zero = "mod_round_to_zero";
Call::ConstString Call::call_cached_indirect_function = "call_cached_indirect_function";
Call::ConstString Call::prefetch = "prefetch";
Call::ConstString Call::prefetch_2d = "prefetch_2d";
Call::ConstString Call::signed_integer_overflow = "signed_integer_overflow";
Call::ConstString Call::indeterminate_expression = "indeterminate_expression";
Call::ConstString Call::bool_to_mask = "bool_to_mask";
Call::ConstString Call::cast_mask = "cast_mask";
Call::ConstString Call::select_mask = "select_mask";
Call::ConstString Call::extract_mask_element = "extract_mask_element";

Call::ConstString Call::buffer_get_min = "_halide_buffer_get_min";
Call::ConstString Call::buffer_get_max = "_halide_buffer_get_max";
Call::ConstString Call::buffer_get_host = "_halide_buffer_get_host";
Call::ConstString Call::buffer_get_dev = "_halide_buffer_get_dev";
Call::ConstString Call::buffer_get_host_dirty = "_halide_buffer_get_host_dirty";
Call::ConstString Call::buffer_get_dev_dirty = "_halide_buffer_get_dev_dirty";
Call::ConstString Call::buffer_set_host_dirty = "_halide_buffer_set_host_dirty";
Call::ConstString Call::buffer_set_dev_dirty = "_halide_buffer_set_dev_dirty";
Call::ConstString Call::buffer_init = "_halide_buffer_init";
Call::ConstString Call::trace = "halide_trace_helper";

}
}
