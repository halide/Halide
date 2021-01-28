#include <iostream>
#include "MCTS.h"
#include "CPU_State.h"

using Halide::Internal::Autoscheduler::CPU_Action;
using Halide::Internal::Autoscheduler::CPU_State;

int main(void) {
    auto solver = MCTS::Solver<CPU_State, CPU_Action>::MakeIterationSolver(10);
    std::cerr << "Before generation" << std::endl;
    CPU_State start(nullptr, nullptr, nullptr, nullptr, 0);
    std::cerr << "After generation" << std::endl;
    solver.print();
    auto solution = solver.solve(start);
    auto finished = solver.get_optimal_state(start, solution);
    std::cout << "Done." << std::endl;
    return 0;
}