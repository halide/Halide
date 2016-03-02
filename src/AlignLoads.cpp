#include "AlignLoads.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Scope.h"
#include "Bounds.h"
#include "ModulusRemainder.h"

namespace Halide {
namespace Internal {

class AlignLoads : public IRMutator {
public:
    AlignLoads(Target t) : target(t) {}
private:
    Target target;

    /** Alignment info for Int(32) variables in scope. */
    Scope<ModulusRemainder> alignment_info;
    using IRMutator::visit;
    ModulusRemainder get_alignment_info(Expr e) {
        return modulus_remainder(e, alignment_info);
    }
    void visit(const Load *op) {
        debug(4) << "AlignLoads: Working on " << (Expr) op << "..\n";
        Expr index = mutate(op->index);
        if (op->type.is_vector()) {
            // int reqd_alignment = target.natural_vector_size(op->type);
            bool external = op->param.defined() || op->image.defined();
            if (external) {
                debug(4) << "AlignLoads: Not dealing with an external image\n";
                debug(4) << (Expr) op << "\n";
                expr = op;
                return;
            }
            else {
                const Ramp *ramp = index.as<Ramp>();
                const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;
                if (ramp && stride && stride->value == 1) {
                    int lanes = ramp->lanes;
                    // At this point we are satisfied that we are loading a native vector.
                    ModulusRemainder mod_rem = get_alignment_info(ramp->base);
                    if (mod_rem.modulus == 1 &&
                        mod_rem.remainder == 0) {
                        // We know nothing about alignment. Give up.
                        // TODO: Fix this.
                        debug(4) << "AlignLoads: Cannot reason about alignment.\n";
                        debug(4) << "Type: " << op->type << "\n";
                        debug(4) << "Index: " << index << "\n";
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
                        if (mod_rem.remainder == 0) {
                            // This is a perfectly aligned address. Nothing to do here.
                            debug(4) << "AlignLoads: Encountered a perfectly aligned load.\n";
                            debug(4) << "Type: " << op->type << "\n";
                            debug(4) << "Index: " << index << "\n";
                            expr = op;
                            return;
                        } else {
                            debug(4) << "AlignLoads: Unaligned load.\n";
                            debug(4) << "Type: " << op->type << "\n";
                            debug(4) << "Index: " << index << "\n";
                            expr = op;
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

Stmt align_loads(Stmt s, const Target &t) {
    return AlignLoads(t).mutate(s);
  }
}
}
