#include "HexagonAlignLoads.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Bounds.h"
#include "ModulusRemainder.h"
#include "Simplify.h"
namespace Halide {
namespace Internal {
using std::vector;

class HexagonAlignLoads : public IRMutator {
public:
    HexagonAlignLoads(Target t) : target(t) {}
private:
    Target target;

    /** Alignment info for Int(32) variables in scope. */
    Scope<ModulusRemainder> alignment_info;
    using IRMutator::visit;
    ModulusRemainder get_alignment_info(Expr e) {
        return modulus_remainder(e, alignment_info);
    }
    void visit(const Load *op) {
        debug(4) << "HexagonAlignLoads: Working on " << (Expr) op << "..\n";
        Expr index = mutate(op->index);
        if (op->type.is_vector()) {
            bool external = op->image.defined();
            if (external) {
                debug(4) << "HexagonAlignLoads: Not dealing with an external image\n";
                debug(4) << (Expr) op << "\n";
                expr = op;
                return;
            }
            else {
                const Ramp *ramp = index.as<Ramp>();
                const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;
                // We will work only on natural vectors supported by the target.
                if (ramp->lanes != target.natural_vector_size(op->type)) {
                    expr = op;
                    return;
                }
                if (ramp && stride && stride->value == 1) {
                    int lanes = ramp->lanes;
                    int vec_size = target.natural_vector_size(Int(8));
                    // If this is a parameter, the base_alignment should be host_alignment.
                    // This cannot be an external image because we have already checked for
                    // it. Otherwise, this is an internal buffers that is always aligned
                    // to the natural vector width.
                    int base_alignment = op->param.defined() ?
                        op->param.host_alignment() : vec_size;

                    int base_lanes_off = mod_imp(base_alignment, lanes);
                    // We reason only in terms of lanes. Each lane is a vector element.
                    // We want to know the following.
                    //    1. if the base + ramp->base (i.e. the index) are aligned.
                    //    2. if not, then how many lanes off an aligned address are they.
                    //    3. if 2, then we create two loads and slice_vector them.

                    // We know the base itself puts us off an aligned address by base_lanes_off
                    // number of lanes.
                    ModulusRemainder mod_rem = get_alignment_info(ramp->base);
                    if (mod_rem.modulus == 1 &&
                        mod_rem.remainder == 0) {
                        // We know nothing about alignment. Give up.
                        // TODO: Fix this.
                        debug(4) << "HexagonAlignLoads: Cannot reason about alignment.\n";
                        debug(4) << "HexagonAlignLoads: Type: " << op->type << "\n";
                        debug(4) << "HexagonAlignLoads: Index: " << index << "\n";
                        expr = op;
                        return;
                    }
                    int base_mod = base_lanes_off + mod_imp(mod_rem.modulus, lanes);
                    int rem_mod = mod_imp(mod_rem.remainder, lanes);
                    if (!(base_mod + rem_mod)) {
                        expr = op;
                        debug(4) << "HexagonAlignLoads: Encountered a perfectly aligned load.\n";
                        debug(4) << "HexagonAlignLoads: Type: " << op->type << "\n";
                        debug(4) << "HexagonAlignLoads: Index: " << index << "\n";
                        return;
                    } else {
                        Expr base = ramp->base;
                        const Add *add = base.as<Add>();
                        const IntImm *b = add->b.as<IntImm>();
                        if (!b) {
                            // Should we be expecting the index to be in simplified
                            // canonical form, i.e. expression + IntImm?
                            debug(4) << "HexagonAlignLoads: add->b is not a constant\n";
                            debug(4) << "HexagonAlignLoads: Type: " << op->type << "\n";
                            debug(4) << "HexagonAlignLoads: Index: " << index << "\n";
                            expr = op;
                            return;
                        }
                        Expr base_low = base - (base_mod + rem_mod);
                        Expr ramp_low = Ramp::make(simplify(base_low), 1, lanes);
                        Expr ramp_high = Ramp::make(simplify(base_low + lanes), 1, lanes);
                        Expr load_low = Load::make(op->type, op->name, ramp_low, op->image, op->param);
                        Expr load_high = Load::make(op->type, op->name, ramp_high, op->image, op->param);
                        // slice_vector considers the vectors in it to be concatenated and the result
                        // is a vector containing as many lanes as the last argument and the vector
                        // begins at lane number specified by the penultimate arg.
                        // So, slice_vector(2, a, b, 126, 128) gives a vector of 128 lanes starting
                        // from lanes 126 through 253 of the concatenated vector a.b.
                        debug(4) << "HexagonAlignLoads: Unaligned Load: Converting " << (Expr) op << " into ...\n";
                        expr = Call::make(op->type, Call::slice_vector, { make_two(Int(32)), load_low, load_high, make_const(Int(32), (base_mod + rem_mod)),
                                    make_const(Int(32), lanes) }, Call::PureIntrinsic);
                        debug(4) <<  "... " << expr << "\n";
                        return;
                    }
                } else if (ramp && stride && stride->value == 2) {
                    // We'll try to break this into two dense loads followed by a shuffle.
                    Expr base_a = ramp->base, base_b = ramp->base + ramp->lanes;
                    bool shifted_b = false;
                    int lanes = ramp->lanes;

                    if (op->param.defined()) {
                        // We need to be able to figure out if buffer_base + base_a is aligned. If not,
                        // we may have to shift base_b left by one so as to not read beyond the end
                        // of an external buffer.
                        ModulusRemainder mod_rem = get_alignment_info(base_a);
                        int base_lanes_off = mod_imp(op->param.host_alignment(), lanes) +
                            mod_imp(mod_rem.modulus, lanes);
                        int rem_mod = mod_imp(mod_rem.remainder, lanes);
                        if (base_lanes_off + rem_mod) {
                            debug(4) << "HexagonAlignLoads: base_a is unaligned: shifting base_b\n";
                            debug(4) << "HexagonAlignLoads: Type: " << op->type << "\n";
                            debug(4) << "HexagonAlignLoads: Index: " << index << "\n";
                            base_b -= 1;
                            shifted_b = true;
                        }
                    }

                    Expr ramp_a = Ramp::make(base_a, 1, lanes);
                    Expr ramp_b = Ramp::make(base_b, 1, lanes);
                    Expr vec_a = mutate(Load::make(op->type, op->name, ramp_a, op->image, op->param));
                    Expr vec_b = mutate(Load::make(op->type, op->name, ramp_b, op->image, op->param));
                    Expr dbl_vec = Call::make(op->type.with_lanes(lanes*2), Call::concat_vectors, { vec_a, vec_b }, Call::PureIntrinsic);

                    std::vector<Expr> args;
                    args.push_back(dbl_vec);
                    for (int i = 0; i < lanes/2; ++i) {
                        args.push_back(make_const(Int(32), i*2));
                    }
                    for (int i = lanes/2; i < lanes; ++i) {
                        args.push_back(make_const(Int(32), i*2 + (shifted_b ? 1 : 0)));
                    }

                    debug(4) << "HexagonAlignLoads: Unaligned Load: Converting " << (Expr) op << " into ...\n";

                    expr = Call::make(op->type, Call::shuffle_vector, args, Call::PureIntrinsic);

                    debug(4) <<  "... " << expr << "\n";
                    return;
                }else {
                    expr = op;
                    return;
                }
            }
        } else {
            expr = op;
            return;
        }
    }

    template<typename NodeType, typename LetType>
    void visit_let(NodeType &result, const LetType *op) {
        Expr value;
        if (op->value.type() == Int(32)) {
            alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
            value = op->value;
        } else {
            const Load *ld = op->value.template as<Load>();
            if (ld) value = mutate(op->value);
            else value = op->value;
        }

        NodeType body = mutate(op->body);
        if (op->value.type() == Int(32)) {
            alignment_info.pop(op->name);
        }
        result = LetType::make(op->name, value, body);
    }

    void visit(const Let *op) { visit_let(expr, op); }
    void visit(const LetStmt *op) { visit_let(stmt, op); }
};

Stmt hexagon_align_loads(Stmt s, const Target &t) {
    return HexagonAlignLoads(t).mutate(s);
  }
}
}
