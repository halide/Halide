#ifndef INTERPRETER_H_
#define INTERPRETER_H_

#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "model.h"

namespace interpret_nn {

struct ScheduleOptions {
    // How much parallelism to enable.
    int parallelism = 0;

    // How much memory to try to fit the working set into.
    int target_working_set_size_bytes = 0;

    // Whether to dump information during scheduling.
    bool verbose = false;
};

// The schedule is a list of ops with crops to run the ops on.
struct ScheduledOp {
    Op *op;
    Box crop;
};

class ModelInterpreter {
    Model model_;

    std::vector<ScheduledOp> schedule_;

    void Schedule(ScheduleOptions options);

public:
    explicit ModelInterpreter(Model m, ScheduleOptions options = ScheduleOptions())
        : model_(std::move(m)) {
        Schedule(options);
    }

    // Return the Tensor in the current Model with the given name.
    // If none with that name, return null. Tensor is still owned by the Model.
    Tensor *get_tensor(const std::string &name);

    void execute();

    // Return the Tensor(s) that are the initial input(s) of the Model.
    // Tensor(s) are still owned by the Model.
    std::vector<Tensor *> inputs();

    // Return the Tensor(s) that are the final output(s) of the Model.
    // Tensor(s) are still owned by the Model.
    std::vector<Tensor *> outputs();

    // Movable but not copyable.
    ModelInterpreter() = delete;
    ModelInterpreter(const ModelInterpreter &) = delete;
    ModelInterpreter &operator=(const ModelInterpreter &) = delete;
    ModelInterpreter(ModelInterpreter &&) = default;
    ModelInterpreter &operator=(ModelInterpreter &&) = default;
};

}  // namespace interpret_nn

#endif  // INTERPRETER_H_
