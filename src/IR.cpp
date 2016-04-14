#include "IR.h"
#include "IRPrinter.h"
#include "IRVisitor.h"

namespace Halide {
namespace Internal {

namespace {

const IntImm *make_immortal_int(int x) {
    IntImm *i = new IntImm;
    i->ref_count.increment();
    i->type = Int(32);
    i->value = x;
    return i;
}

}

const IntImm *IntImm::small_int_cache[] = {make_immortal_int(-8),
                                           make_immortal_int(-7),
                                           make_immortal_int(-6),
                                           make_immortal_int(-5),
                                           make_immortal_int(-4),
                                           make_immortal_int(-3),
                                           make_immortal_int(-2),
                                           make_immortal_int(-1),
                                           make_immortal_int(0),
                                           make_immortal_int(1),
                                           make_immortal_int(2),
                                           make_immortal_int(3),
                                           make_immortal_int(4),
                                           make_immortal_int(5),
                                           make_immortal_int(6),
                                           make_immortal_int(7),
                                           make_immortal_int(8)};


Expr Cast::make(Type t, Expr v) {
    internal_assert(v.defined()) << "Cast of undefined\n";
    internal_assert(t.lanes() == v.type().lanes()) << "Cast may not change vector widths\n";

    Cast *node = new Cast;
    node->type = t;
    node->value = v;
    return node;
}

Expr Add::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Add of undefined\n";
    internal_assert(b.defined()) << "Add of undefined\n";
    internal_assert(a.type() == b.type()) << "Add of mismatched types\n";

    Add *node = new Add;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Sub::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Sub of undefined\n";
    internal_assert(b.defined()) << "Sub of undefined\n";
    internal_assert(a.type() == b.type()) << "Sub of mismatched types\n";

    Sub *node = new Sub;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Mul::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Mul of undefined\n";
    internal_assert(b.defined()) << "Mul of undefined\n";
    internal_assert(a.type() == b.type()) << "Mul of mismatched types\n";

    Mul *node = new Mul;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Div::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Div of undefined\n";
    internal_assert(b.defined()) << "Div of undefined\n";
    internal_assert(a.type() == b.type()) << "Div of mismatched types\n";

    Div *node = new Div;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Mod::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Mod of undefined\n";
    internal_assert(b.defined()) << "Mod of undefined\n";
    internal_assert(a.type() == b.type()) << "Mod of mismatched types\n";

    Mod *node = new Mod;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Min::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Min of undefined\n";
    internal_assert(b.defined()) << "Min of undefined\n";
    internal_assert(a.type() == b.type()) << "Min of mismatched types\n";

    Min *node = new Min;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr Max::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "Max of undefined\n";
    internal_assert(b.defined()) << "Max of undefined\n";
    internal_assert(a.type() == b.type()) << "Max of mismatched types\n";

    Max *node = new Max;
    node->type = a.type();
    node->a = a;
    node->b = b;
    return node;
}

Expr EQ::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "EQ of undefined\n";
    internal_assert(b.defined()) << "EQ of undefined\n";
    internal_assert(a.type() == b.type()) << "EQ of mismatched types\n";

    EQ *node = new EQ;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}

Expr NE::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "NE of undefined\n";
    internal_assert(b.defined()) << "NE of undefined\n";
    internal_assert(a.type() == b.type()) << "NE of mismatched types\n";

    NE *node = new NE;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}

Expr LT::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "LT of undefined\n";
    internal_assert(b.defined()) << "LT of undefined\n";
    internal_assert(a.type() == b.type()) << "LT of mismatched types\n";

    LT *node = new LT;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}


Expr LE::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "LE of undefined\n";
    internal_assert(b.defined()) << "LE of undefined\n";
    internal_assert(a.type() == b.type()) << "LE of mismatched types\n";

    LE *node = new LE;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}

Expr GT::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "GT of undefined\n";
    internal_assert(b.defined()) << "GT of undefined\n";
    internal_assert(a.type() == b.type()) << "GT of mismatched types\n";

    GT *node = new GT;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}


Expr GE::make(Expr a, Expr b) {
    internal_assert(a.defined()) << "GE of undefined\n";
    internal_assert(b.defined()) << "GE of undefined\n";
    internal_assert(a.type() == b.type()) << "GE of mismatched types\n";

    GE *node = new GE;
    node->type = Bool(a.type().lanes());
    node->a = a;
    node->b = b;
    return node;
}

Expr And::make(Expr a, Expr b) {
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

Expr Or::make(Expr a, Expr b) {
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

Expr Not::make(Expr a) {
    internal_assert(a.defined()) << "Not of undefined\n";
    internal_assert(a.type().is_bool()) << "argument of Not is not a bool\n";

    Not *node = new Not;
    node->type = Bool(a.type().lanes());
    node->a = a;
    return node;
}

Expr Select::make(Expr condition, Expr true_value, Expr false_value) {
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

Expr Load::make(Type type, std::string name, Expr index, Buffer image, Parameter param) {
    internal_assert(index.defined()) << "Load of undefined\n";
    internal_assert(type.lanes() == index.type().lanes()) << "Vector lanes of Load must match vector lanes of index\n";

    Load *node = new Load;
    node->type = type;
    node->name = name;
    node->index = index;
    node->image = image;
    node->param = param;
    return node;
}

Expr Ramp::make(Expr base, Expr stride, int lanes) {
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

Expr Broadcast::make(Expr value, int lanes) {
    internal_assert(value.defined()) << "Broadcast of undefined\n";
    internal_assert(value.type().is_scalar()) << "Broadcast of vector\n";
    internal_assert(lanes != 1) << "Broadcast of lanes 1\n";

    Broadcast *node = new Broadcast;
    node->type = value.type().with_lanes(lanes);
    node->value = value;
    node->lanes = lanes;
    return node;
}

Expr Let::make(std::string name, Expr value, Expr body) {
    internal_assert(value.defined()) << "Let of undefined\n";
    internal_assert(body.defined()) << "Let of undefined\n";

    Let *node = new Let;
    node->type = body.type();
    node->name = name;
    node->value = value;
    node->body = body;
    return node;
}

Stmt LetStmt::make(std::string name, Expr value, Stmt body) {
    internal_assert(value.defined()) << "Let of undefined\n";
    internal_assert(body.defined()) << "Let of undefined\n";

    LetStmt *node = new LetStmt;
    node->name = name;
    node->value = value;
    node->body = body;
    return node;
}

Stmt AssertStmt::make(Expr condition, Expr message) {
    internal_assert(condition.defined()) << "AssertStmt of undefined\n";
    internal_assert(message.type() == Int(32)) << "AssertStmt message must be an int:" << message << "\n";

    AssertStmt *node = new AssertStmt;
    node->condition = condition;
    node->message = message;
    return node;
}

Stmt ProducerConsumer::make(std::string name, Stmt produce, Stmt update, Stmt consume) {
    internal_assert(produce.defined()) << "ProducerConsumer of undefined\n";
    // update is allowed to be null
    internal_assert(consume.defined()) << "ProducerConsumer of undefined\n";

    ProducerConsumer *node = new ProducerConsumer;
    node->name = name;
    node->produce = produce;
    node->update = update;
    node->consume = consume;
    return node;
}

Stmt For::make(std::string name, Expr min, Expr extent, ForType for_type, DeviceAPI device_api, Stmt body) {
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

Stmt Store::make(std::string name, Expr value, Expr index, Parameter param) {
    internal_assert(value.defined()) << "Store of undefined\n";
    internal_assert(index.defined()) << "Store of undefined\n";

    Store *node = new Store;
    node->name = name;
    node->value = value;
    node->index = index;
    node->param = param;
    return node;
}

Stmt Provide::make(std::string name, const std::vector<Expr> &values, const std::vector<Expr> &args) {
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

Stmt Allocate::make(std::string name, Type type, const std::vector<Expr> &extents,
                    Expr condition, Stmt body,
                    Expr new_expr, std::string free_function) {
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

Stmt Free::make(std::string name) {
    Free *node = new Free;
    node->name = name;
    return node;
}

Stmt Realize::make(const std::string &name, const std::vector<Type> &types, const Region &bounds, Expr condition, Stmt body) {
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

Stmt Block::make(Stmt first, Stmt rest) {
    internal_assert(first.defined()) << "Block of undefined\n";
    // rest is allowed to be null

    Block *node = new Block;
    node->first = first;
    node->rest = rest;
    return node;
}

Stmt IfThenElse::make(Expr condition, Stmt then_case, Stmt else_case) {
    internal_assert(condition.defined() && then_case.defined()) << "IfThenElse of undefined\n";
    // else_case may be null.

    IfThenElse *node = new IfThenElse;
    node->condition = condition;
    node->then_case = then_case;
    node->else_case = else_case;
    return node;
}

Stmt Evaluate::make(Expr v) {
    internal_assert(v.defined()) << "Evaluate of undefined\n";

    Evaluate *node = new Evaluate;
    node->value = v;
    return node;
}

Expr Call::make(Type type, std::string name, const std::vector<Expr> &args, CallType call_type,
                Function func, int value_index,
                Buffer image, Parameter param) {
    for (size_t i = 0; i < args.size(); i++) {
        internal_assert(args[i].defined()) << "Call of undefined\n";
    }
    if (call_type == Halide) {
        internal_assert(value_index >= 0 &&
                        value_index < func.outputs())
            << "Value index out of range in call to halide function\n";
        internal_assert((func.has_pure_definition() || func.has_extern_definition()))
            << "Call to undefined halide function\n";
        internal_assert((int)args.size() <= func.dimensions())
            << "Call node with too many arguments.\n";
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

Expr Variable::make(Type type, std::string name, Buffer image, Parameter param, ReductionDomain reduction_domain) {
    internal_assert(!name.empty());
    Variable *node = new Variable;
    node->type = type;
    node->name = name;
    node->image = image;
    node->param = param;
    node->reduction_domain = reduction_domain;
    return node;
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

template<> IRNodeType ExprNode<IntImm>::_type_info = {};
template<> IRNodeType ExprNode<UIntImm>::_type_info = {};
template<> IRNodeType ExprNode<FloatImm>::_type_info = {};
template<> IRNodeType ExprNode<StringImm>::_type_info = {};
template<> IRNodeType ExprNode<Cast>::_type_info = {};
template<> IRNodeType ExprNode<Variable>::_type_info = {};
template<> IRNodeType ExprNode<Add>::_type_info = {};
template<> IRNodeType ExprNode<Sub>::_type_info = {};
template<> IRNodeType ExprNode<Mul>::_type_info = {};
template<> IRNodeType ExprNode<Div>::_type_info = {};
template<> IRNodeType ExprNode<Mod>::_type_info = {};
template<> IRNodeType ExprNode<Min>::_type_info = {};
template<> IRNodeType ExprNode<Max>::_type_info = {};
template<> IRNodeType ExprNode<EQ>::_type_info = {};
template<> IRNodeType ExprNode<NE>::_type_info = {};
template<> IRNodeType ExprNode<LT>::_type_info = {};
template<> IRNodeType ExprNode<LE>::_type_info = {};
template<> IRNodeType ExprNode<GT>::_type_info = {};
template<> IRNodeType ExprNode<GE>::_type_info = {};
template<> IRNodeType ExprNode<And>::_type_info = {};
template<> IRNodeType ExprNode<Or>::_type_info = {};
template<> IRNodeType ExprNode<Not>::_type_info = {};
template<> IRNodeType ExprNode<Select>::_type_info = {};
template<> IRNodeType ExprNode<Load>::_type_info = {};
template<> IRNodeType ExprNode<Ramp>::_type_info = {};
template<> IRNodeType ExprNode<Broadcast>::_type_info = {};
template<> IRNodeType ExprNode<Call>::_type_info = {};
template<> IRNodeType ExprNode<Let>::_type_info = {};
template<> IRNodeType StmtNode<LetStmt>::_type_info = {};
template<> IRNodeType StmtNode<AssertStmt>::_type_info = {};
template<> IRNodeType StmtNode<ProducerConsumer>::_type_info = {};
template<> IRNodeType StmtNode<For>::_type_info = {};
template<> IRNodeType StmtNode<Store>::_type_info = {};
template<> IRNodeType StmtNode<Provide>::_type_info = {};
template<> IRNodeType StmtNode<Allocate>::_type_info = {};
template<> IRNodeType StmtNode<Free>::_type_info = {};
template<> IRNodeType StmtNode<Realize>::_type_info = {};
template<> IRNodeType StmtNode<Block>::_type_info = {};
template<> IRNodeType StmtNode<IfThenElse>::_type_info = {};
template<> IRNodeType StmtNode<Evaluate>::_type_info = {};

Call::ConstString Call::debug_to_file = "debug_to_file";
Call::ConstString Call::shuffle_vector = "shuffle_vector";
Call::ConstString Call::interleave_vectors = "interleave_vectors";
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
Call::ConstString Call::rewrite_buffer = "rewrite_buffer";
Call::ConstString Call::create_buffer_t = "create_buffer_t";
Call::ConstString Call::copy_buffer_t = "copy_buffer_t";
Call::ConstString Call::extract_buffer_host = "extract_buffer_host";
Call::ConstString Call::extract_buffer_min = "extract_buffer_min";
Call::ConstString Call::extract_buffer_max = "extract_buffer_max";
Call::ConstString Call::set_host_dirty = "set_host_dirty";
Call::ConstString Call::set_dev_dirty = "set_dev_dirty";
Call::ConstString Call::popcount = "popcount";
Call::ConstString Call::count_leading_zeros = "count_leading_zeros";
Call::ConstString Call::count_trailing_zeros = "count_trailing_zeros";
Call::ConstString Call::undef = "undef";
Call::ConstString Call::address_of = "address_of";
Call::ConstString Call::null_handle = "null_handle";
Call::ConstString Call::trace = "trace";
Call::ConstString Call::trace_expr = "trace_expr";
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
Call::ConstString Call::copy_memory = "copy_memory";
Call::ConstString Call::likely = "likely";
Call::ConstString Call::make_int64 = "make_int64";
Call::ConstString Call::make_float64 = "make_float64";
Call::ConstString Call::register_destructor = "register_destructor";
Call::ConstString Call::div_round_to_zero = "div_round_to_zero";
Call::ConstString Call::mod_round_to_zero = "mod_round_to_zero";


}
}
