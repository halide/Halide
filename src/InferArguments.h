#ifndef HALIDE_INFER_ARGUMENTS_H
#define HALIDE_INFER_ARGUMENTS_H

#include <vector>

#include "Argument.h"
#include "Buffer.h"
#include "Expr.h"
#include "Parameter.h"

/** \file
 *
 * Interface for a visitor to infer arguments used in a body Stmt.
 */

namespace Halide {
namespace Internal {

/** An inferred argument. Inferred args are either Params,
 * ImageParams, or Buffers. The first two are handled by the param
 * field, and global images are tracked via the buf field. These
 * are used directly when jitting, or used for validation when
 * compiling with an explicit argument list. */
struct InferredArgument {
    Argument arg;
    Parameter param;
    Buffer<> buffer;

    bool operator<(const InferredArgument &other) const {
        if (arg.is_buffer() && !other.arg.is_buffer()) {
            return true;
        } else if (other.arg.is_buffer() && !arg.is_buffer()) {
            return false;
        } else {
            return arg.name < other.arg.name;
        }
    }
};

class Function;

std::vector<InferredArgument> infer_arguments(Stmt body, const std::vector<Function> &outputs);

}  // namespace Internal
}  // namespace Halide

#endif
