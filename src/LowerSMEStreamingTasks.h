#ifndef HALIDE_LOWER_SME_STREAMING_TASKS_H
#define HALIDE_LOWER_SME_STREAMING_TASKS_H

/** \file
 * Defines a lowering pass to pull loops marked with SMEStreaming device API to a separate function.
 * In aarch64 SME, an execution can switch to 'streaming mode' by smstart/smstop instruction.
 * In LLVM, a function with special attributes is compiled so it transits to/from streaming mode.
 * In Halide, to follow this mechanism, a For loop with sme_streaming enabled is extracted
 * as a separated function called streaming task to have the different attribute in CodeGen.
 * We also handle the case where a streaming task has a non-streaming loop in it.
 */

#include <string>
#include <vector>

namespace Halide {

struct Target;

namespace Internal {

struct Stmt;
struct LoweredFunc;

/**
 * Create a separate function that executes the body as a streaming (or non-streaming) task.
 * Inject a Call op to call the extracted task function.
 * The extracted task functions are appended to closure_implementations.
 */
Stmt lower_sme_streaming_tasks(const Stmt &s, std::vector<LoweredFunc> &closure_implementations,
                               const std::string &name, const Target &t);

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_LOWER_SME_STREAMING_TASKS_H
