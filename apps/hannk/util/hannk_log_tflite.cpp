#include <cstring>

#include "tensorflow/lite/tools/logging.h"
#include "util/hannk_log.h"

namespace hannk {
namespace internal {

void hannk_log(LogSeverity severity, const char *msg) {
    // TFLite will append std::endl, so back up to ignore any that we append
    size_t len = strlen(msg);
    while (len > 0 && msg[len - 1] == '\n') {
        --len;
    }
    switch (severity) {
    case INFO:
        TFLITE_LOG(INFO).write(msg, len);
        break;
    case WARNING:
        TFLITE_LOG(WARN).write(msg, len);
        break;
    case ERROR:
        TFLITE_LOG(ERROR).write(msg, len);
        break;
    case FATAL:
        TFLITE_LOG(FATAL).write(msg, len);
        break;
    }
}

}  // namespace internal
}  // namespace hannk
