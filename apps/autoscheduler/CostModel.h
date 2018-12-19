#ifndef COST_MODEL_H
#define COST_MODEL_H

#include <functional>

#include "HalideBuffer.h"

class CostModel {
public:
    struct Params {
        std::string weights_dir;
        std::string weights_server_hostname;
        int weights_server_port = 0;
        int weights_server_experiment_id = 0;
        bool randomize_weights_on_load = false;

        Params() = default;

        explicit Params(std::function<const char *(const char *)> env) {
            if (const char *e = env("HL_WEIGHTS_DIR")) {
                weights_dir = e;
            }
            if (const char *e = env("HL_WEIGHTS_SERVER_HOSTNAME")) {
                weights_server_hostname = e;
            }
            if (!weights_server_hostname.empty()) {
                if (const char *e = env("HL_WEIGHTS_SERVER_PORT")) {
                    weights_server_port = std::atoi(e);
                }
                if (const char *e = env("HL_WEIGHTS_SERVER_EXPERIMENT_ID")) {
                    weights_server_experiment_id = std::atoi(e);
                }
            }
            if (const char *e = env("HL_RANDOMIZE_WEIGHTS")) {
                randomize_weights_on_load = std::string(e) == "1";
            }
        }
    };

    virtual ~CostModel() = default;
    virtual void set_pipeline_features(const Halide::Runtime::Buffer<float> &pipeline_feats, int n) = 0;
    virtual void enqueue(int ns, Halide::Runtime::Buffer<float> *schedule_feats, double *cost_ptr) = 0;
    virtual void evaluate_costs() = 0;
    virtual void reset() = 0;
    virtual float backprop(const Halide::Runtime::Buffer<const float> &true_runtimes, float learning_rate) = 0;
    virtual void save_weights() = 0;
    static std::unique_ptr<CostModel> make_default(const Params &p);
};

#endif
