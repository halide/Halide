#include "contrib/sampler.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <random>
#include <utility>
#include <vector>

#include "HalideBuffer.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "contrib/status_helpers.h"

namespace hallmark {

absl::StatusOr<std::unique_ptr<Sampler>> Sampler::Create(Type type, int top_k,
                                                         float top_p,
                                                         float temperature,
                                                         int seed) {
    if (type == Type::kTopK || type == Type::kTopP) {
        if (top_k <= 1) {
            return absl::InvalidArgumentError("top_k must be > 1");
        } else if (temperature < 0.0f) {
            return absl::InvalidArgumentError("temperature must be >= 0");
        }
        if (type == Type::kTopP && (top_p <= 0 || top_p > 1.0)) {
            return absl::InvalidArgumentError("top_p must be between 0 and 1");
        }
    }
    return std::unique_ptr<Sampler>(
        new Sampler(type, top_k, top_p, temperature, seed));
}

absl::StatusOr<std::vector<int>> Sampler::Sample(
    Halide::Runtime::Buffer<float> &logits) {
    if (logits.dimensions() != 3 || logits.dim(1).extent() != 1) {
        return absl::InvalidArgumentError(
            "Buffer must be (vocab_size, 1 [seq_len], Batch)");
    }

    switch (type_) {
    case Type::kGreedy:
        return SampleGreedy(logits);
    case Type::kTopK:
        return SampleTopK(logits);
    case Type::kTopP:
        return SampleTopP(logits);
    default:
        return absl::InvalidArgumentError("Unsupported sampler type");
    }
};

Sampler::Sampler(Type type, int top_k, float top_p, float temperature, int seed)
    : type_(type),
      top_k_(top_k),
      top_p_(top_p),
      temperature_(temperature),
      generator_(std::make_unique<std::mt19937>(seed)) {
}

absl::StatusOr<std::vector<int>> Sampler::SampleGreedy(
    Halide::Runtime::Buffer<float> &logits) {
    const int vocab_size = logits.dim(0).extent();
    const int seq_pos = logits.dim(1).min();
    const int batch_size = logits.dim(2).extent();

    std::vector<int> outputs;
    outputs.reserve(batch_size);
    // select the token with the highest logit directly.
    for (int c = 0; c < batch_size; ++c) {
        float max_logit = logits(0, seq_pos, c);
        int max_id = 0;
        for (int v = 0; v < vocab_size; ++v) {
            const float prob = logits(v, seq_pos, c);
            if (prob > max_logit) {
                max_logit = prob;
                max_id = v;
            }
        }
        outputs.push_back(max_id);
    }
    return outputs;
};

absl::StatusOr<std::vector<int>> Sampler::SampleTopK(
    Halide::Runtime::Buffer<float> &logits) {
    const int vocab_size = logits.dim(0).extent();
    const int seq_pos = logits.dim(1).min();
    const int batch_size = logits.dim(2).extent();

    std::vector<int> outputs;
    outputs.reserve(batch_size);
    for (int batch = 0; batch < batch_size; ++batch) {
        std::vector<std::pair<float, int>> logits_ids;
        logits_ids.reserve(vocab_size);
        for (int v = 0; v < vocab_size; ++v) {
            const float logit = logits(v, seq_pos, batch);
            logits_ids.push_back(std::make_pair(logit, v));
        }
        RETURN_IF_ERROR(SelectTopK(logits_ids, top_k_));
        // No need to normalize logits here, sampler takes care of that.
        RETURN_IF_ERROR(ScaledSoftmax(logits_ids, /*normalize=*/false));
        auto sample_idx = DoSampling(logits_ids);
        if (!sample_idx.ok()) {
            return sample_idx.status();
        }
        outputs.push_back(sample_idx.value());
    }
    return outputs;
}

absl::StatusOr<std::vector<int>> Sampler::SampleTopP(
    Halide::Runtime::Buffer<float> &logits) {
    const int vocab_size = logits.dim(0).extent();
    const int seq_pos = logits.dim(1).min();
    const int batch_size = logits.dim(2).extent();
    const int k = top_k_ > 0 ? top_k_ : vocab_size;

    std::vector<int> outputs;
    outputs.reserve(batch_size);
    for (int batch = 0; batch < batch_size; ++batch) {
        std::vector<std::pair<float, int>> logits_ids;
        logits_ids.reserve(vocab_size);
        for (int v = 0; v < vocab_size; ++v) {
            const float logit = logits(v, seq_pos, batch);
            logits_ids.push_back(std::make_pair(logit, v));
        }
        RETURN_IF_ERROR(SelectTopK(logits_ids, k));
        RETURN_IF_ERROR(ScaledSoftmax(logits_ids, /*normalize=*/true));
        RETURN_IF_ERROR(SelectTopP(logits_ids, top_p_));
        auto sample_idx = DoSampling(logits_ids);
        if (!sample_idx.ok()) {
            return sample_idx.status();
        }
        outputs.push_back(sample_idx.value());
    }
    return outputs;
}

absl::Status Sampler::SelectTopK(std::vector<std::pair<float, int>> &logits_ids,
                                 int k) {
    if (k > logits_ids.size()) {
        return absl::InvalidArgumentError(
            "Top k value must be smaller than the number of logits.");
    }
    std::partial_sort(
        logits_ids.begin(), logits_ids.begin() + k, logits_ids.end(),
        [](const std::pair<float, int> &a, const std::pair<float, int> &b) {
            // reverse order.
            return a.first > b.first;
        });
    logits_ids.resize(k);
    return absl::OkStatus();
}

absl::Status Sampler::SelectTopP(std::vector<std::pair<float, int>> &logits_ids,
                                 float p) {
    int included = 0;
    float prob_sum = 0.0;
    for (const auto &[logit, _] : logits_ids) {
        ++included;
        prob_sum += logit;
        if (prob_sum >= p) {
            break;
        }
    }
    if (included == 0) {
        return absl::InternalError("Bad top_p value.");
    }
    logits_ids.resize(included);
    return absl::OkStatus();
}

absl::Status Sampler::ScaledSoftmax(
    std::vector<std::pair<float, int>> &logits_ids, bool normalize) {
    float scale = 1 / (temperature_ ? temperature_ : 1.0);
    double sum = 0.0;
    float max_logit = logits_ids[0].first;
    for (int i = 0; i < logits_ids.size(); ++i) {
        const float logit = logits_ids[i].first;
        const float p = expf(scale * (logit - max_logit));
        sum += p;
        logits_ids[i].first = p;
    }
    if (normalize) {
        for (int i = 0; i < logits_ids.size(); ++i) {
            logits_ids[i].first /= sum;
        }
    }
    return absl::OkStatus();
}

absl::StatusOr<int> Sampler::DoSampling(
    std::vector<std::pair<float, int>> &logits_ids) {
    std::vector<float> probs;
    probs.reserve(logits_ids.size());
    for (const auto &[logit, _] : logits_ids) {
        probs.push_back(logit);
    }
    // Probabilities are normalized by `discrete_distribution`.
    std::discrete_distribution<> dist(probs.begin(), probs.end());
    int sample_idx = dist(*generator_);
    return logits_ids[sample_idx].second;
}

}  // namespace hallmark
