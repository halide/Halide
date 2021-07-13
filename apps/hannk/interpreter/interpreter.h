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
    std::shared_ptr<OpGroup> model_;
    std::unique_ptr<char[]> tensor_storage_arena_;

    void init(InterpreterOptions options);

public:
    explicit Interpreter(std::shared_ptr<OpGroup> m, InterpreterOptions options = InterpreterOptions());
    ~Interpreter();

    // Return the Tensor in the current Model with the given name.
    // If none with that name, return null. Tensor is still owned by the Model.
    TensorPtr get_tensor(const std::string &name);

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
