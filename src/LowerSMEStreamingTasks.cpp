#include "LowerSMEStreamingTasks.h"

#include "Argument.h"
#include "Closure.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IRPrinter.h"
#include "InjectHostDevBufferCopies.h"

namespace Halide {
namespace Internal {

namespace {
constexpr int DBG = 2;

LoweredArgument make_scalar_arg(const std::string &name, const Type &type) {
    return LoweredArgument(name, Argument::Kind::InputScalar, type, 0, ArgumentEstimates());
}

template<typename T>
LoweredArgument make_scalar_arg(const std::string &name) {
    return make_scalar_arg(name, type_of<T>());
}

struct LowerSMEStreamingTasks : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *loop) override {
        if (loop->device_api != DeviceAPI::None) {
            internal_assert(loop->device_api == DeviceAPI::Host || loop->device_api == DeviceAPI::SMEStreaming);
            // After this mutation, it doesn't need to be marked as SMEStreaming anymore
            Stmt body;
            if (equal(loop->min, loop->max)) {
                body = LetStmt::make(loop->name, loop->min, loop->body);
            } else {
                body = For::make(loop->name, loop->min, loop->max, loop->for_type, loop->partition_policy,
                                 DeviceAPI::None, loop->body);
            }

            const bool next_is_streaming = (loop->device_api == DeviceAPI::SMEStreaming);
            // We extract a separate task only when transiting to/from SMEStreaming
            // 1. Any(except for SMEStreaming) -> SMEStreaming
            // 2. SMEStreaming -> Host
            if (in_streaming != next_is_streaming) {
                debug(DBG) << "Switching to " << to_streaming_str(next_is_streaming)
                           << " from " << to_streaming_str(in_streaming)
                           << " in loop " << loop->name << "\n";

                ScopedValue<bool> streaming_state(in_streaming, next_is_streaming);

                auto s = do_as_streaming_task(body, loop->name);
                return s;
            } else {
                return mutate(body);
            }
        }

        return IRMutator::visit(loop);
    }

    // Create a separate function that executes the body as a streaming (or non-streaming) task.
    // Inject a Call op to call the extracted task function.
    // The extracted task function is added to closure_implementations, which will be added to Module.
    Stmt do_as_streaming_task(Stmt &body, const std::string &name) {
        auto task_name = unique_name(concat_strings(name, ".", to_streaming_str(in_streaming), ".task"));

        Closure closure;
        debug(DBG) << "Closure include for " << task_name << "\n"
                   << body << "\n";
        closure.include(body);

        // The same name can appear as a var and a buffer. Remove the var name in this case.
        for (auto const &b : closure.buffers) {
            closure.vars.erase(b.first);
        }

        const std::string closure_name = unique_name("streaming_closure");
        const std::string closure_arg_name = unique_name("closure_arg");
        Expr closure_struct_allocation = closure.pack_into_struct();
        Expr closure_struct = Variable::make(Handle(), closure_name);
        Expr closure_struct_arg = Cast::make(type_of<uint8_t *>(), closure_struct);
        auto closure_arg = make_scalar_arg<uint8_t *>(closure_arg_name);
        Expr closure_arg_var = Variable::make(closure_struct_allocation.type(), closure_arg_name);

        // Mutate body recursively, where further transition may happen
        body = mutate(body);
        Stmt wrapped_body = closure.unpack_from_struct(closure_arg_var, body);

        const std::string new_function_name = c_print_name(task_name, false);
        auto attributes = in_streaming ? LoweredFunc::Attribute::SME_STREAMING_TASK :
                                         LoweredFunc::Attribute::SME_NONSTREAMING_TASK;
        LoweredFunc closure_func{new_function_name,
                                 std::vector<LoweredArgument>{std::move(closure_arg)},
                                 std::move(wrapped_body),
                                 LinkageType::Internal,
                                 NameMangling::C,
                                 attributes};
        closure_implementations.emplace_back(std::move(closure_func));

        Stmt stmt = call_extern_and_assert(new_function_name, {std::move(closure_struct_arg)});
        stmt = LetStmt::make(closure_name, closure_struct_allocation, stmt);
        return stmt;
    }

    std::string to_streaming_str(bool is_streaming) const {
        return is_streaming ? "streaming" : "nonstreaming";
    }

    LowerSMEStreamingTasks() = default;

    bool in_streaming = false;
    std::vector<LoweredFunc> closure_implementations;
};

}  // namespace

Stmt lower_sme_streaming_tasks(const Stmt &s, std::vector<LoweredFunc> &closure_implementations,
                               const std::string &name, const Target &) {

    LowerSMEStreamingTasks lowering_mutator;
    Stmt result = lowering_mutator(s);

    // Main body will be dumped as part of standard lowering debugging, but closures will not be.
    debug(2) << [&] {
        std::stringstream ss;
        for (const auto &lf : lowering_mutator.closure_implementations) {
            ss << "lower_sme_streaming_tasks generated closure lowered function " << lf.name << ":\n"
               << lf.body << "\n\n";
        }
        return ss.str();
    }();

    // Append to the end rather than replacing the list entirely.
    closure_implementations.insert(closure_implementations.end(),
                                   lowering_mutator.closure_implementations.begin(),
                                   lowering_mutator.closure_implementations.end());

    return result;
}

}  // namespace Internal
}  // namespace Halide
