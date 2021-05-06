#include "delegate/hannk_delegate.h"
#include "tensorflow/lite/tools/delegates/delegate_provider.h"
#include "util/error_util.h"

namespace tflite {
namespace tools {

class HannkDelegateProvider : public DelegateProvider {
public:
    HannkDelegateProvider() {
        default_params_.AddParam("use_hannk", ToolParam::Create<bool>(false));
        default_params_.AddParam("hannk_verbosity", ToolParam::Create<int>(0));
    }

    std::vector<Flag> CreateFlags(ToolParams *params) const final {
        std::vector<Flag> flags = {
            CreateFlag<bool>("use_hannk", params, "use HANNK"),
            CreateFlag<int>("hannk_verbosity", params, "Verbosity of HANNK debug logging"),
        };
        return flags;
    }

    void LogParams(const ToolParams &params, bool verbose) const final {
        LOG_TOOL_PARAM(params, bool, "use_hannk", "Use HANNK", verbose);
        LOG_TOOL_PARAM(params, int, "hannk_verbosity", "HANNK verbosity", verbose);
    }

    TfLiteDelegatePtr CreateTfLiteDelegate(const ToolParams &params) const final {
        if (params.Get<bool>("use_hannk")) {
            HannkDelegateOptions options = {};
            options.verbosity = params.Get<int32_t>("hannk_verbosity");
            if (options.verbosity >= 1) {
                LOG(INFO) << "Registrar HannkDelegate: verbosity set to "
                          << options.verbosity << ".";
            }
            return TfLiteDelegatePtr(HannkDelegateCreate(&options), &HannkDelegateDelete);
        } else {
            return TfLiteDelegatePtr(nullptr, [](TfLiteDelegate *) {});
        }
    }

    std::string GetName() const final {
        return "HANNK";
    }
};
REGISTER_DELEGATE_PROVIDER(HannkDelegateProvider);

}  // namespace tools
}  // namespace tflite
