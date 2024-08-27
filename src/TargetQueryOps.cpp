#include "TargetQueryOps.h"

#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

namespace {

class LowerTargetQueryOps : public IRMutator {
    const Target &t;

    using IRMutator::visit;

    Expr visit(const Call *call) override {
        if (call->is_intrinsic(Call::target_arch_is)) {
            Target::Arch arch = (Target::Arch)*as_const_int(call->args[0]);
            return make_bool(t.arch == arch);
        } else if (call->is_intrinsic(Call::target_has_feature)) {
            Target::Feature feat = (Target::Feature)*as_const_int(call->args[0]);
            return make_bool(t.has_feature(feat));
        } else if (call->is_intrinsic(Call::target_natural_vector_size)) {
            Expr zero = call->args[0];
            return Expr(t.natural_vector_size(zero.type()));
        } else if (call->is_intrinsic(Call::target_os_is)) {
            Target::OS os = (Target::OS)*as_const_int(call->args[0]);
            return make_bool(t.os == os);
        } else if (call->is_intrinsic(Call::target_bits)) {
            return Expr(t.bits);
        }

        return IRMutator::visit(call);
    }

public:
    LowerTargetQueryOps(const Target &t)
        : t(t) {
    }
};

}  // namespace

void lower_target_query_ops(std::map<std::string, Function> &env, const Target &t) {
    for (auto &iter : env) {
        Function &func = iter.second;
        LowerTargetQueryOps ltqo(t);
        func.mutate(&ltqo);
    }
}

}  // namespace Internal
}  // namespace Halide
