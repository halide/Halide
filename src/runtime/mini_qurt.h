#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "printer.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Qurt {
enum { QURT_EOK = 0 };
}}}} // namespace Halide::Runtime::Internal::Qurt

typedef unsigned int qurt_size_t;          /**< QuRT size type. */
typedef unsigned int qurt_mem_pool_t;      /**< QuRT emory pool type.*/

