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
    HexagonAlignLoads(Target t, int vs) : target(t), vector_size(vs) {}
private:
    Target target;
    enum AlignCheck { Aligned, Unaligned, NoResult };
    // size of a vector in bytes.
    int vector_size;
    // Alignment info for Int(32) variables in scope.
    Scope<ModulusRemainder> alignment_info;
    using IRMutator::visit;
    ModulusRemainder get_alignment_info(Expr e) {
        return modulus_remainder(e, alignment_info);
    }
    int natural_vector_lanes(Type t) {
        return vector_size / t.bytes();
    }

    Expr concat_and_shuffle(Expr vec_a, Expr vec_b, std::vector<Expr> &indices) {
        Type dbl_t = vec_a.type().with_lanes(vec_a.type().lanes() * 2);
        Expr dbl_vec = Call::make(dbl_t, Call::concat_vectors, { vec_a, vec_b }, Call::PureIntrinsic);
        std::vector<Expr> args;
        args.push_back(dbl_vec);
        for (int i = 0; i < (int) indices.size(); i++) {
            args.push_back(indices[i]);
        }
        return Call::make(vec_a.type(), Call::shuffle_vector, args, Call::PureIntrinsic);
    }
    Expr concat_and_shuffle(Expr vec_a, Expr vec_b, int start, int size) {
        std::vector<Expr> args;
        for (int i = start; i < start+size; i++) {
            args.push_back(i);
        }
        return concat_and_shuffle(vec_a, vec_b, args);
    }

    AlignCheck get_alignment_check(const Ramp *ramp, int host_alignment, int *lanes_off) {
        // We reason only in terms of lanes. Each lane is a vector element.
        // We want to know the following.
        //    1. if the base of buffer + ramp->base (i.e. the index) are aligned.
        //    2. if not, then how many lanes off an aligned address are they (returned in base_mod).
        //    3. if 2, then we create two loads and slice_vector them.
        //    4. rem_mod is used if the ramp base is 64*x + 65 and lanes is 64, then we are not
        //       modulus_remainder.remainder lanes off, but we are only 1 lane off.
        int lanes = ramp->lanes;
        Expr base = ramp->base;
        // We know the base itself puts us off an aligned address by base_lanes_off
        // number of lanes.
        int base_lanes_off = mod_imp(host_alignment, lanes);
        ModulusRemainder mod_rem = get_alignment_info(base);
        if (mod_rem.modulus == 1 && mod_rem.remainder == 0) {
            // We can't reason about alignment.
            return AlignCheck::NoResult;
        } else {
            int base_mod = base_lanes_off + mod_imp(mod_rem.modulus, lanes);
            int rem_mod = mod_imp(mod_rem.remainder, lanes);
            if (!(base_mod + rem_mod)) {
                return AlignCheck::Aligned;
            } else {
                if (lanes_off) *lanes_off = base_mod + rem_mod;
                return AlignCheck::Unaligned;
            }
        }
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
            } else {
                const Ramp *ramp = index.as<Ramp>();
                const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;
                // We will work only on natural vectors supported by the target.
                if (ramp->lanes != natural_vector_lanes(op->type)) {
                    expr = op;
                    return;
                }
                if (ramp && stride && stride->value == 1) {
                    int lanes = ramp->lanes;
                    // If this is a parameter, the base_alignment should be host_alignment.
                    // This cannot be an external image because we have already checked for
                    // it. Otherwise, this is an internal buffers that is always aligned
                    // to the natural vector width.
                    int base_alignment =
                        op->param.defined() ? op->param.host_alignment() : vector_size;
                    int lanes_off;
                    AlignCheck ac = get_alignment_check(ramp, base_alignment, &lanes_off);
                    if (ac == AlignCheck::Unaligned) {
                        Expr base = ramp->base;
                        Expr base_low = base - (lanes_off);
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
                        expr = concat_and_shuffle(load_low, load_high, lanes_off, lanes);
                        debug(4) <<  "... " << expr << "\n";
                        return;
                    } else {
                        debug(4) <<  "HexagonAlignLoads: " << ((ac == AlignCheck::Aligned) ? "Aligned Load" : "Cannot reason about alignment");
                        debug(4) << "HexagonAlignLoads: Type: " << op->type << "\n";
                        debug(4) << "HexagonAlignLoads: Index: " << index << "\n";
                        expr = op;
                        return;
                    }
                } else if (ramp && stride && stride->value == 2) {
                    // We'll try to break this into two dense loads followed by a shuffle.
                    Expr base_a = ramp->base, base_b = ramp->base + ramp->lanes;
                    int b_shift = 0;
                    int lanes = ramp->lanes;

                    if (op->param.defined()) {
                        // We need to be able to figure out if buffer_base + base_a is aligned. If not,
                        // we may have to shift base_b left by one so as to not read beyond the end
                        // of an external buffer.
                        AlignCheck ac = get_alignment_check(ramp, op->param.host_alignment(), nullptr);

                        if (ac == AlignCheck::Unaligned) {
                            debug(4) << "HexagonAlignLoads: base_a is unaligned: shifting base_b\n";
                            debug(4) << "HexagonAlignLoads: Type: " << op->type << "\n";
                            debug(4) << "HexagonAlignLoads: Index: " << index << "\n";
                            base_b -= 1;
                            b_shift = 1;
                        }
                    }

                    Expr ramp_a = Ramp::make(base_a, 1, lanes);
                    Expr ramp_b = Ramp::make(base_b, 1, lanes);
                    Expr vec_a = mutate(Load::make(op->type, op->name, ramp_a, op->image, op->param));
                    Expr vec_b = mutate(Load::make(op->type, op->name, ramp_b, op->image, op->param));

                    std::vector<Expr> indices;
                    for (int i = 0; i < lanes/2; ++i) {
                        indices.push_back(i*2);
                    }
                    for (int i = lanes/2; i < lanes; ++i) {
                        indices.push_back(i*2 + b_shift);
                    }

                    debug(4) << "HexagonAlignLoads: Unaligned Load: Converting " << (Expr) op << " into ...\n";

                    expr = concat_and_shuffle(vec_a, vec_b, indices);

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
        if (op->value.type() == Int(32)) {
            alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
        }

        Expr value = mutate(op->value);
        NodeType body = mutate(op->body);

        if (op->value.type() == Int(32)) {
            alignment_info.pop(op->name);
        }
        result = LetType::make(op->name, value, body);
    }

    void visit(const Let *op) { visit_let(expr, op); }
    void visit(const LetStmt *op) { visit_let(stmt, op); }
    void visit(const For *op) {
        int saved_vector_size = vector_size;
        if (op->device_api == DeviceAPI::Hexagon) {
            if (target.has_feature(Target::HVX_128)) {
                vector_size = 128;
            }
            else if (target.has_feature(Target::HVX_64)) {
                vector_size = 64;
            } else {
                internal_error << "Unknown HVX mode";
            }
        }
        IRMutator::visit(op);
        vector_size = saved_vector_size;
        return;
    }
};

Stmt hexagon_align_loads(Stmt s, const Target &t) {
    return HexagonAlignLoads(t, t.natural_vector_size(Int(8))).mutate(s);
  }
}
}
