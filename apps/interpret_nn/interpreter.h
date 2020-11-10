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
};

class ModelInterpreter {
    Model *model_;

    // The schedule is a list of ops with crops to run the ops on.
    struct ScheduledOp {
        Op *op;
        CropShape crop;
    };
    std::vector<ScheduledOp> schedule_;

    // Can ops a and b be reordered with respect to each other?
    static bool CanReorder(const ScheduledOp &a, const ScheduledOp &b);

    // Compute the cost of the memory accesses between a producer from and
    // a consumer to. The cost is related to the size of the memory accessed,
    // and the distance between the regions of memory accessed.
    static float Distance(const ScheduledOp &from, const ScheduledOp &to);

    void ScheduleNaive();
    void Schedule(ScheduleOptions options);

public:
    explicit ModelInterpreter(Model *m,
                              ScheduleOptions options = ScheduleOptions())
        : model_(m) {
        Schedule(options);
    }

    Tensor *GetTensor(const std::string &name) {
        return nullptr;
    }

    void Execute();

    // Movable but not copyable.
    ModelInterpreter() = delete;
    ModelInterpreter(const ModelInterpreter &) = delete;
    ModelInterpreter &operator=(const ModelInterpreter &) = delete;
    ModelInterpreter(ModelInterpreter &&) = default;
    ModelInterpreter &operator=(ModelInterpreter &&) = default;};

}  // namespace interpret_nn

#endif  // INTERPRETER_H_
