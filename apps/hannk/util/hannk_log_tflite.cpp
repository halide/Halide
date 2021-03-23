#include "tensorflow/lite/tools/logging.h"
#include "util/hannk_log.h"

namespace hannk {
namespace internal {

void hannk_log(LogSeverity severity, const char *msg) {
    switch (severity) {
    case INFO:
        TFLITE_LOG(INFO) << msg;
        break;
    case WARNING:
        TFLITE_LOG(WARN) << msg;
        break;
    case ERROR:
        TFLITE_LOG(ERROR) << msg;
        break;
    case FATAL:
        TFLITE_LOG(FATAL) << msg;
        break;
    }
}

}  // namespace internal
}  // namespace hannk
