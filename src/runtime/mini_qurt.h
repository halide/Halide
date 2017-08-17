#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "printer.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Qurt {

enum { QURT_EOK = 0 };
typedef unsigned int qurt_addr_t;          /**< QuRT address type.*/
typedef unsigned int qurt_size_t;          /**< QuRT size type. */
typedef unsigned int qurt_mem_pool_t;      /**< QuRT emory pool type.*/
typedef unsigned int qurt_mem_region_t;    /**< QuRT memory regions type. */


}}}} // namespace Halide::Runtime::Internal::Qurt
