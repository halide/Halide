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
            // int reqd_alignment = target.natural_vector_size(op->type);
            bool external = op->param.defined() || op->image.defined();
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
                    // At this point we are satisfied that we are loading a native vector.
                    // So to gauge alignment we require the index to be a mulitple of lanes.
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
                    // ModulusRemainder tells us if something can be written in the
                    // form
                    // (ModulusRemainder.modulus * c1) + ModulusRemainder.remainder.
                    // So for us to be able to generate an aligned load, ramp->base
                    // should be
                    // (lanes * c1) + c2.
                    if (!(mod_rem.modulus % lanes)) {
                        int offset_elements = mod_imp(mod_rem.remainder, lanes);
                        if (mod_rem.remainder == 0 ||
                            offset_elements == 0) {
                            // The first case is obvious.
                            // The second handles ramps such as
                            // ramp(64 * c1 + 128, 1, 64)
                            // This is a perfectly aligned address. Nothing to do here.
                            debug(4) << "HexagonAlignLoads: Encountered a perfectly aligned load.\n";
                            debug(4) << "HexagonAlignLoads: Type: " << op->type << "\n";
                            debug(4) << "HexagonAlignLoads: Index: " << index << "\n";
                            expr = op;
                            return;
                        } else {
                            debug(4) << "HexagonAlignLoads: Unaligned load.\n";
                            debug(4) << "HexagonAlignLoads: Type: " << op->type << "\n";
                            debug(4) << "HexagonAlignLoads: Index: " << index << "\n";
                            // We can generate two aligned loads followed by a shuffle_vectors if the
                            // base is like so
                            // 1. (aligned_expr + const)
                            // So, we have the following conditions.
                            // (mod_rem.modulus % alignment_required) = 0
                            // mod_rem.remainder = mod_imp(const, alignment_required)
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
                            int b_val = b->value;
                            // If the index is A + b, then we know that A is already aligned. We need
                            // to know if b, which is an IntImm also contains an aligned vector inside.
                            // For e.g. if b is 65 and lanes is 64, then we have 1 aligned vector in it.
                            // and base_low should be (A + 64)
                            int offset_vector = div_imp(b_val, lanes) * lanes;
                            Expr base_low = simplify(add->a + offset_vector);
                            Expr base_high = simplify(base_low + lanes);
                            Expr ramp_low = Ramp::make(base_low, 1, lanes);
                            Expr ramp_high = Ramp::make(base_high, 1, lanes);
                            Expr load_low = Load::make(op->type, op->name, ramp_low, op->image, op->param);
                            Expr load_high = Load::make(op->type, op->name, ramp_high, op->image, op->param);
                            Expr two = IntImm::make(Int(32), 2);
                            Expr start_lane = IntImm::make(Int(32), offset_elements);
                            Expr num_lanes = IntImm::make(Int(32), lanes);
                            vector<Expr> args = {two, load_low, load_high, start_lane, num_lanes};
                            debug(4) << "HexagonAlignLoads: Replacing with two aligned loads and slice_vector\n";
                            debug(4) << "HexagonAlignLoads: load_low  : " << load_low << "\n";
                            debug(4) << "HexagonAlignLoads: load_high : " << load_high << "\n";
                            expr = Call::make(op->type, Call::slice_vector, args, Call::Intrinsic);
                            debug(4) << "HexagonAlignLoads: slice_vector : " << expr << "\n";
                            return;
                        } // (mod_rem.remainder != 0)
                    } else {
                        expr = op;
                        return;
                    }
                 } else {
                    // (ramp && stride && stride->value != 1)
                    // expr = Load::make(op->type, op->name, index, op->image, op->param);
                    expr = op;
                    return;
                }
            } //if (external) else
        } else {
          //(!op->type.is_vector()
            // expr = Load::make(op->type, op->name, index, op->image, op->param);
            expr = op;
            return;
        }
    }
    // void visit(const Let *op) {
    //     if (op->value.type() == Int(32)) {
    //         alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
    //     }
    //     Expr body = mutate(op->body);
    //     if (op->value.type() == Int(32)) {
    //         alignment_info.pop(op->name);
    //     }
    //     expr = Let::make(op->name, op->value, body);
    // }
    // void visit(const LetStmt *op) {
    //     if (op->value.type() == Int(32)) {
    //         alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
    //     }

    //     Stmt body = mutate(op->body);

    //     if (op->value.type() == Int(32)) {
    //         alignment_info.pop(op->name);
    //     }
    //     stmt = LetStmt::make(op->name, op->value, body);
    // }

};

Stmt hexagon_align_loads(Stmt s, const Target &t) {
    return HexagonAlignLoads(t).mutate(s);
  }
}
}
