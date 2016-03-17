#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "printer.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Qurt {

enum { QURT_EOK = 0 };

template <typename T>
bool get_qurt_symbol(void *user_context, const char *name, T &sym) {
    if (!sym) {
        sym = (T)halide_get_symbol(name);
    }
    if (!sym) {
        error(user_context) << "QuRT symbol '" << name << "' not found.\n";
        return false;
    }
    return true;
}

}}}} // namespace Halide::Runtime::Internal::Qurt
