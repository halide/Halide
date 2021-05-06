#include "util/hannk_log.h"

#include <cstdio>
#include <cstdlib>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace hannk {
namespace internal {

namespace {

#if defined(__ANDROID__)
int const android_severity[] = {ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR, ANDROID_LOG_FATAL};
#endif

}  // namespace

void hannk_log(LogSeverity severity, const char *msg) {
    fprintf(stderr, "%s", msg);
    if (severity == FATAL) {
        fflush(stderr);
        std::abort();
    }
#if defined(__ANDROID__)
    __android_log_write(android_severity[(int)severity], "hannk", msg);
#endif
}

}  // namespace internal
}  // namespace hannk
