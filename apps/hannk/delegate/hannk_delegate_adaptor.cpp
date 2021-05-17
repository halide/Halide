#include <sstream>

#include "delegate/hannk_delegate.h"
#include "tensorflow/lite/c/c_api.h"
#include "util/error_util.h"

namespace {

template<typename T>
bool ParseValue(const char *in, T &result) {
    std::istringstream stream(in);
    stream >> result;
    if (!stream.eof() && !stream.good()) {
        return false;
    }
    return true;
}

bool ParseOptions(char **options_keys,
                  char **options_values,
                  size_t num_options,
                  HannkDelegateOptions *options) {
    HannkDelegateOptionsDefault(options);

    for (size_t i = 0; i < num_options; ++i) {
        if (!strcmp(options_keys[i], "verbosity")) {
            if (!ParseValue(options_values[i], options->verbosity)) {
                HLOG(WARNING) << "ParseOptions: malformed option " << options_keys[i] << "\n";
                return false;
            }
        } else {
            HLOG(WARNING) << "ParseOptions: unknown option " << options_keys[i] << "\n";
            return false;
        }
    }

    return true;
}

}  // namespace

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Defines two symbols that need to be exported to use the TFLite external
// delegate. See tensorflow/lite/delegates/external for details.
TFL_CAPI_EXPORT TfLiteDelegate *tflite_plugin_create_delegate(char **options_keys,
                                                              char **options_values,
                                                              size_t num_options,
                                                              void (*report_error)(const char *)) {
    HannkDelegateOptions options;
    if (!ParseOptions(options_keys, options_values, num_options, &options)) {
        return nullptr;
    }

    if (options.verbosity >= 1) {
        HLOG(INFO) << "External HannkDelegate: verbosity set to "
                   << options.verbosity << ".";
    }

    return HannkDelegateCreate(&options);
}

TFL_CAPI_EXPORT void tflite_plugin_destroy_delegate(TfLiteDelegate *delegate) {
    HannkDelegateDelete(delegate);
}

#ifdef __cplusplus
}
#endif  // __cplusplus
