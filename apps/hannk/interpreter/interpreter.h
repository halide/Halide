#ifndef HANNK_INTERPRETER_H
#define HANNK_INTERPRETER_H

#include <string>
#include <vector>

#include "interpreter/model.h"

namespace hannk {

struct InterpreterOptions {
    // Verbosity level. 0 = None.
    int verbosity = 0;

    // Whether to enable tracing.
    bool trace = false;
};

class Interpreter {
    OpPtr model_;
    std::unique_ptr<char[]> tensor_storage_arena_;
    InterpreterOptions options_;
    bool prepared_ = false;

public:
    explicit Interpreter(OpPtr m, InterpreterOptions options = InterpreterOptions());
    ~Interpreter();

    // Return the Tensor in the current Model with the given name.
    // If none with that name, return null. Tensor is still owned by the Model.
    TensorPtr get_tensor(const std::string &name);

    // Must call prepare() exactly once, before any calls to execute().
    // This performs various transformations on the ops, and allows
    // ops chance to prepare for execution; this is a good
    // time for the op to prepare and cache anything that might be used
    // repeatedly if execute() is called multiple times. (Note that an op may have
    // prepare() called on it, but then later get discarded by a transform.)
    //
    // Returns false if an error occurs, in which case execute() should not be called.
    [[nodiscard]] bool prepare();

    void execute();

    // Return the Tensor(s) that are the initial input(s) of the Model.
    std::vector<TensorPtr> inputs();

    // Return the Tensor(s) that are the final output(s) of the Model.
    std::vector<TensorPtr> outputs();

    // Movable but not copyable.
    Interpreter() = delete;
    Interpreter(const Interpreter &) = delete;
    Interpreter &operator=(const Interpreter &) = delete;
    Interpreter(Interpreter &&) = default;
    Interpreter &operator=(Interpreter &&) = default;
};

}  // namespace hannk

#endif  // HANNK_INTERPRETER_H
