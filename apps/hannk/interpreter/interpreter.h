#ifndef HANNK_INTERPRETER_H
#define HANNK_INTERPRETER_H

#include <string>
#include <vector>

#include "interpreter/model.h"

namespace hannk {

struct InterpreterOptions {
    // Whether to dump information during scheduling.
    bool verbose = false;

    // Whether to enable tracing.
    bool trace = false;
};

class Interpreter {
    std::unique_ptr<OpGroup> model_;

    void init(InterpreterOptions options);

public:
    explicit Interpreter(std::unique_ptr<OpGroup> m, InterpreterOptions options = InterpreterOptions());
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
