#include "runtime_internal.h"
#include "HalideRuntimeQurt.h"
#include "printer.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Qurt {

enum qurt_hvx_mode_t {
    QURT_HVX_MODE_64B = 0,
    QURT_HVX_MODE_128B = 1,
};

enum { QURT_EOK = 0 };

typedef int (*qurt_hvx_lock_t)(int mode);
typedef int (*qurt_hvx_unlock_t)();

WEAK qurt_hvx_lock_t qurt_hvx_lock = NULL;
WEAK qurt_hvx_unlock_t qurt_hvx_unlock = NULL;

template <typename T>
T get_qurt_symbol(void *user_context, const char *name) {
    T s = (T)halide_get_symbol(name);
    if (!s) {
        error(user_context) << "QuRT symbol '" << name << "' not found.\n";
    }
    return s;
}

}}}} // namespace Halide::Runtime::Internal::Qurt

using namespace Halide::Runtime::Internal::Qurt;

extern "C" {

WEAK int halide_qurt_hvx_lock(void *user_context, int size) {
    if (!qurt_hvx_lock || !qurt_hvx_unlock) {
        qurt_hvx_lock = get_qurt_symbol<qurt_hvx_lock_t>(user_context, "qurt_hvx_lock");
        qurt_hvx_unlock = get_qurt_symbol<qurt_hvx_unlock_t>(user_context, "qurt_hvx_unlock");
        if (!qurt_hvx_lock || !qurt_hvx_unlock) {
            return -1;
        }
    }

    qurt_hvx_mode_t mode;
    switch (size) {
    case 64: mode = QURT_HVX_MODE_64B; break;
    case 128: mode = QURT_HVX_MODE_128B; break;
    default:
        error(user_context) << "HVX lock size must be 64 or 128.\n";
        return -1;
    }

    debug(user_context) << "QuRT: qurt_hvx_lock(" << mode << ") ->\n";
    int result = qurt_hvx_lock(mode);
    debug(user_context) << "        " << result;
    if (result != QURT_EOK) {
        error(user_context) << "qurt_hvx_lock failed\n";
        return -1;
    }

    return 0;
}

WEAK int halide_qurt_hvx_unlock(void *user_context) {
    if (!qurt_hvx_lock || !qurt_hvx_unlock) {
        error(user_context) << "qurt_hvx_unlock must follow a successful call to qurt_hvx_lock.\n";
        return -1;
    }

    debug(user_context) << "QuRT: qurt_hvx_unlock ->\n";
    int result = qurt_hvx_unlock();
    debug(user_context) << "        " << result;
    if (result != QURT_EOK) {
        error(user_context) << "qurt_hvx_unlock failed\n";
        return -1;
    }

    return 0;
}

}
