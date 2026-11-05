#include "LoadInvariantBits.h"
#include "Definition.h"
#include "ExprUsesVar.h"
#include "Function.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Qualify.h"
#include "Simplify.h"
#include "Substitute.h"

#include <algorithm>

namespace Halide {
namespace Internal {

namespace {

// The mask of the bits of var (in [0, 2^bits)) that leave args unchanged.
uint64_t invariant_bits(const std::vector<Expr> &args, const std::string &var, int bits) {
    const uint64_t all = (uint64_t{1} << bits) - 1;
    if (std::none_of(args.begin(), args.end(), [&](const Expr &a) { return expr_uses_var(a, var); })) {
        return all;
    }
    // args at each value of var, simplified on first use
    std::vector<std::vector<Expr>> at(size_t{1} << bits);
    auto args_at = [&](int x) -> const std::vector<Expr> & {
        if (at[x].empty()) {
            for (const Expr &a : args) {
                at[x].push_back(simplify(substitute(var, x, a)));
            }
        }
        return at[x];
    };
    uint64_t mask = 0;
    for (int b = 0; b < bits; b++) {
        bool invariant = true;
        for (int x = 0; invariant && x < (1 << bits); x++) {
            if (x >> b & 1) {
                continue;
            }
            const std::vector<Expr> &a = args_at(x), &c = args_at(x | 1 << b);
            for (size_t i = 0; invariant && i < a.size(); i++) {
                // (not can_prove: its CSE would consume unique names)
                invariant = equal(a[i], c[i]) || is_const_one(simplify(a[i] == c[i]));
            }
        }
        mask |= (uint64_t)invariant << b;
    }
    return mask;
}

// Inlines the calls to Funcs that can be inlined, binding their args with
// Lets named as Inliner's, without its CSE (which would consume unique names
// and so perturb the names of everything lowered later).
class InlineAll : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->call_type != Call::Halide || !op->func.defined() || !Function(op->func).can_be_inlined()) {
            return IRMutator::visit(op);
        }
        Function f(op->func);
        Expr body = mutate(qualify(f.name() + ".", f.values()[op->value_index]));
        for (size_t i = 0; i < op->args.size(); i++) {
            body = Let::make(f.name() + "." + f.args()[i], mutate(op->args[i]), body);
        }
        return body;
    }
};

}  // namespace

std::vector<uint64_t> load_invariant_bits(const Definition &d, const std::string &var, int bits) {
    internal_assert(bits >= 0 && bits < 32) << "load_invariant_bits enumerates 2^" << bits << " values\n";
    std::vector<uint64_t> masks;
    for (Expr e : d.values()) {
        e = remove_promises(remove_likelies(substitute_in_all_lets(InlineAll()(e))));
        visit_with(e, [&](auto *self, const Call *c) {
            auto is_load = [](const Call *c) {
                return c && (c->call_type == Call::Image || c->call_type == Call::Halide);
            };
            if (const Call *s = c->is_intrinsic(Call::struct_field_read) ? c->args[0].as<Call>() : nullptr; is_load(s)) {
                std::vector<Expr> args = s->args;
                args.insert(args.end(), c->args.begin() + 1, c->args.end());
                masks.push_back(invariant_bits(args, var, bits));
                for (const Expr &a : args) {
                    a.accept(self);
                }
                return;
            }
            if (is_load(c)) {
                masks.push_back(invariant_bits(c->args, var, bits));
            }
            self->visit_base(c);
        });
    }
    return masks;
}

}  // namespace Internal
}  // namespace Halide
