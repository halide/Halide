#include "CodeGen_GPU_Dev.h"
#include "CanonicalizeGPUVars.h"
#include "CodeGen_Internal.h"
#include "Deinterleave.h"
#include "ExprUsesVar.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
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
                    s->with(mutate(extract_lane(s->value, ln)), mutate(extract_lane(s->index, ln)), const_true(), s->alignment + ln)));
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
                Expr load_expr = op->with(extract_lane(op->index, ln), const_true(), op->alignment + ln);
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
        // Vector shuffle with arbitrary number of lanes per arg
        internal_assert(!op->vectors.empty());
        internal_assert(op->type.lanes() == (int)op->indices.size());

        // Construct the mapping for each shuffled element to find
        // the corresponding vector-index to use and which lane-index
        // of the selected vector.
        auto vector_lane_indices = op->vector_and_lane_indices();

        // Traverse all the vector args
        std::vector<std::string> vecs;
        vecs.reserve(op->vectors.size());
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

        int element_idx = 0;
        for (auto element_mapping : vector_lane_indices) {
            int vector_idx = element_mapping.first;
            int lane_idx = element_mapping.second;

            // Print the vector in which we will index.
            rhs << vecs[vector_idx];

            // In case we are dealing with an actual vector instead of scalar,
            // print out the required indexing syntax.
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

            // Elements of a vector are comma separated.
            if (element_idx < (int)(op->indices.size() - 1)) {
                rhs << ", ";
            }
            element_idx++;
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
    if (op->is_intrinsic(Call::abs)) {
        internal_assert(op->args.size() == 1);
        if (op->type.is_float()) {
            std::stringstream fn;
            fn << "abs_f" << op->type.bits();
            Expr equiv = Call::make(op->type, fn.str(), op->args, Call::PureExtern);
            equiv.accept(this);
        } else {
            // Note: The integer-abs doesn't have suffixes in Halide.
            if (abs_returns_unsigned_type) {
                // Halide also returns unsigned, so we're good. Just replace it
                // with a PureExtern function call.
                Expr abs = Call::make(op->type, "abs", op->args, Call::PureExtern);
                Expr equiv = cast(op->type, abs);
                equiv.accept(this);
            } else {
                // Halide does `unsigned T abs(signed T)`, whereas C and most other
                // APIs do `T abs(T)`. So we have to wrap it in an additional cast.
                Type arg_type = op->args[0].type();
                Expr abs = Call::make(arg_type, "abs", op->args, Call::PureExtern);
                Expr equiv = cast(op->type, abs);
                equiv.accept(this);
            }
        }
    } else if (op->is_intrinsic(Call::strict_fma)) {
        // All shader languages have fma
        Expr equiv = Call::make(op->type, "fma", op->args, Call::PureExtern);
        equiv.accept(this);
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_GPU_C::visit(const Mod *op) {
    if (op->type.is_float()) {
        // All shader languages have fmod
        Expr equiv = Call::make(op->type, "fmod", {op->a, op->b}, Call::PureExtern);
        equiv.accept(this);
    } else {
        CodeGen_C::visit(op);
    }
}

std::string CodeGen_GPU_C::print_extern_call(const Call *op) {
    internal_assert(!function_takes_user_context(op->name)) << op->name;

    // Here we do not scalarize function calls with vector arguments.
    // Backends should provide those functions, and if not available,
    // we could compose them by writing out a call element by element,
    // but that's never happened until 2025, so I guess we can leave
    // this to be an error for now, just like it was.

    std::ostringstream rhs;
    std::vector<std::string> args(op->args.size());
    for (size_t i = 0; i < op->args.size(); i++) {
        args[i] = print_expr(op->args[i]);
    }
    std::string name = op->name;
    auto it = extern_function_name_map.find(name);
    if (it != extern_function_name_map.end()) {
        name = it->second;
        debug(3) << "Rewriting " << op->name << " as " << name << "\n";
    }
    debug(3) << "Writing out call to " << name << "\n";
    rhs << name << "(" << with_commas(args) << ")";
    return rhs.str();
}

std::string add_line_directives(const std::string &source, const std::vector<IRPrinterLocation> &locations,
                                const StmtSourceMap &ir, const std::string &source_name) {
    const StmtSourceMap printed = source_map_of(source, locations);
    const std::vector<const StmtSourceMap::Location *> in_ir = map_locations(printed, ir);

    std::vector<std::string_view> lines;
    for (size_t start = 0; start < source.size();) {
        size_t end = std::min(source.find('\n', start), source.size());
        lines.emplace_back(std::string_view(source).substr(start, end - start));
        start = end + 1;
    }

    // The line of ir each line was made from: that of the innermost Stmt it
    // was printed for. Visit enclosing Stmts first, so that the Stmts in them
    // overwrite their lines.
    std::vector<uint32_t> ir_line(lines.size() + 1, 0);
    std::vector<uint32_t> order(printed.locations.size());
    for (uint32_t i = 0; i < (uint32_t)order.size(); i++) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](uint32_t i, uint32_t j) {
        const StmtSourceMap::Location &a = printed.locations[i], &b = printed.locations[j];
        return a.start != b.start ? a.start < b.start : a.end > b.end;
    });
    for (uint32_t i : order) {
        const StmtSourceMap::Location &l = printed.locations[i];
        if (!in_ir[i]) {
            continue;
        }
        for (uint32_t line = l.line; line <= l.end_line && line <= lines.size(); line++) {
            ir_line[line] = in_ir[i]->line;
        }
    }

    // A #line directive sets the number of the next line, and the lines after
    // count on from it.
    std::ostringstream out;
    uint32_t lines_out = 0, implied = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        const uint32_t want = ir_line[i + 1];
        if (want && implied != want) {
            out << "#line " << want << " \"" << ir.path << "\"\n";
            lines_out++;
        } else if (!want && implied) {
            out << "#line " << lines_out + 2 << " \"" << source_name << "\"\n";
            lines_out++;
        }
        out << lines[i] << "\n";
        lines_out++;
        implied = want ? want + 1 : 0;
    }
    return std::move(out).str();
}

}  // namespace Internal
}  // namespace Halide
