#include "CodeGen_GPU_Dev.h"
#include "CanonicalizeGPUVars.h"
#include "Deinterleave.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"

namespace Halide {
namespace Internal {

CodeGen_GPU_Dev::~CodeGen_GPU_Dev() = default;

namespace {
// Check to see if a buffer is a candidate for constant memory storage.
// A buffer is a candidate for constant memory if it is never written to,
// and loads are uniform within the workgroup.
class IsBufferConstant : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Store *op) override {
        if (op->name == buffer) {
            result = false;
        }
        if (result) {
            IRVisitor::visit(op);
        }
    }

    void visit(const Load *op) override {
        if (op->name == buffer &&
            expr_uses_vars(op->index, depends_on_thread_var)) {
            result = false;
        }
        if (result) {
            IRVisitor::visit(op);
        }
    }

    void visit(const LetStmt *op) override {
        op->value.accept(this);
        ScopedBinding<> bind_if(expr_uses_vars(op->value, depends_on_thread_var),
                                depends_on_thread_var,
                                op->name);
        op->body.accept(this);
    }

    void visit(const Let *op) override {
        op->value.accept(this);
        ScopedBinding<> bind_if(expr_uses_vars(op->value, depends_on_thread_var),
                                depends_on_thread_var,
                                op->name);
        op->body.accept(this);
    }

    void visit(const For *op) override {
        ScopedBinding<> bind_if(op->for_type == ForType::GPUThread ||
                                    op->for_type == ForType::GPULane,
                                depends_on_thread_var,
                                op->name);
        IRVisitor::visit(op);
    }

    Scope<> depends_on_thread_var;

public:
    bool result = true;
    const std::string &buffer;

    IsBufferConstant(const std::string &b)
        : buffer(b) {
    }
};
}  // namespace

bool CodeGen_GPU_Dev::is_buffer_constant(const Stmt &kernel,
                                         const std::string &buffer) {
    IsBufferConstant v(buffer);
    kernel.accept(&v);
    return v.result;
}

namespace {

class ScalarizePredicatedLoadStore : public IRMutator {
public:
    using IRMutator::mutate;
    using IRMutator::visit;

protected:
    Stmt visit(const Store *s) override {
        if (!is_const_one(s->predicate)) {
            std::vector<Stmt> scalar_stmts;
            for (int ln = 0; ln < s->value.type().lanes(); ln++) {
                scalar_stmts.push_back(IfThenElse::make(
                    extract_lane(s->predicate, ln),
                    Store::make(s->name,
                                mutate(extract_lane(s->value, ln)),
                                mutate(extract_lane(s->index, ln)),
                                s->param,
                                const_true(),
                                s->alignment + ln)));
            }
            return Block::make(scalar_stmts);
        } else {
            return s;
        }
    }

    Expr visit(const Load *op) override {
        if (!is_const_one(op->predicate)) {
            std::vector<Expr> lane_values;
            for (int ln = 0; ln < op->type.lanes(); ln++) {
                Expr load_expr = Load::make(op->type.element_of(),
                                            op->name,
                                            extract_lane(op->index, ln),
                                            op->image,
                                            op->param,
                                            const_true(),
                                            op->alignment + ln);
                lane_values.push_back(Call::make(load_expr.type(),
                                                 Call::if_then_else,
                                                 {extract_lane(op->predicate, ln),
                                                  load_expr,
                                                  make_zero(op->type.element_of())},
                                                 Internal::Call::PureIntrinsic));
            }
            Expr pred_load = Shuffle::make_concat(lane_values);
            return pred_load;
        } else {
            return op;
        }
    }
};

}  // namespace

Stmt CodeGen_GPU_Dev::scalarize_predicated_loads_stores(Stmt &s) {
    ScalarizePredicatedLoadStore sps;
    return sps.mutate(s);
}

void CodeGen_GPU_C::visit(const Shuffle *op) {
    if (op->type.is_scalar()) {
        CodeGen_C::visit(op);
    } else {
        internal_assert(!op->vectors.empty());
        for (size_t i = 1; i < op->vectors.size(); i++) {
            internal_assert(op->vectors[0].type() == op->vectors[i].type());
        }
        internal_assert(op->type.lanes() == (int)op->indices.size());
        std::vector<int> vector_first_index;
        int max_index = 0;
        for (const Expr &v : op->vectors) {
            vector_first_index.push_back(max_index);
            max_index += v.type().lanes();
        }
        for (int i : op->indices) {
            internal_assert(i >= 0 && i < max_index);
        }

        std::vector<std::string> vecs;
        for (const Expr &v : op->vectors) {
            vecs.push_back(print_expr(v));
        }

        std::string src = vecs[0];
        std::ostringstream rhs;
        std::string storage_name = unique_name('_');
        switch (vector_declaration_style) {
        case VectorDeclarationStyle::OpenCLSyntax:
            rhs << "(" << print_type(op->type) << ")(";
            break;
        case VectorDeclarationStyle::WGSLSyntax:
            rhs << print_type(op->type) << "(";
            break;
        case VectorDeclarationStyle::CLikeSyntax:
            rhs << "{";
            break;
        }
        int elem_num = 0;
        for (int i : op->indices) {
            size_t vector_idx;
            int lane_idx = -1;
            for (vector_idx = 0; vector_idx < op->vectors.size(); ++vector_idx) {
                if (i >= vector_first_index[vector_idx] && i < vector_first_index[vector_idx] + op->vectors[vector_idx].type().lanes()) {
                    lane_idx = i - vector_first_index[vector_idx];
                    break;
                }
            }
            internal_assert(lane_idx != -1) << "Shuffle lane index not found.";
            rhs << vecs[vector_idx];
            if (op->vectors[vector_idx].type().lanes() > 1) {
                switch (vector_declaration_style) {
                case VectorDeclarationStyle::OpenCLSyntax:
                    rhs << ".s" << lane_idx;
                    break;
                case VectorDeclarationStyle::WGSLSyntax:
                case VectorDeclarationStyle::CLikeSyntax:
                    rhs << "[" << lane_idx << "]";
                    break;
                }
            }
            if (elem_num < (int)(op->indices.size() - 1)) {
                rhs << ", ";
            }
            elem_num++;
        }
        switch (vector_declaration_style) {
        case VectorDeclarationStyle::OpenCLSyntax:
            rhs << ")";
            break;
        case VectorDeclarationStyle::WGSLSyntax:
            rhs << ")";
            break;
        case VectorDeclarationStyle::CLikeSyntax:
            rhs << "}";
            break;
        }
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_GPU_C::visit(const Call *op) {
    // In metal and opencl, "rint" is a polymorphic function that matches our
    // rounding semantics. GLSL handles it separately using "roundEven".
    if (op->is_intrinsic(Call::round)) {
        print_assignment(op->type, "rint(" + print_expr(op->args[0]) + ")");
    } else {
        CodeGen_C::visit(op);
    }
}

}  // namespace Internal
}  // namespace Halide
