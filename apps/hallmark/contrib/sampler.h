#ifndef HALIDE_APPS_HALLMARK_SAMPLER_H_
#define HALIDE_APPS_HALLMARK_SAMPLER_H_

#include <random>

#include "HalideBuffer.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace hallmark {

class Sampler {
public:
    enum class Type { kGreedy,
                      kTopK,
                      kTopP };

    // Creates a Sampler.
    // * If kGreedy sampler is used, Argmax will be returned ignoring all other
    //   arguments provided.
    // * If kTopK sampler is used, the top k logit values are selected. That is
    //   followed by temperature scaling and applying softmax. Finally, a sampled
    //   is drawn from the resulting distribution.
    // * If kTopP sampler is selcted, the top k logits are first selcted if k > 0.
    //   Otherwise, k = vocab size. This is followed by temperature scaling and
    //   applying softmax. Finally, the top p are selcted from the probabilities
    //   such that sum of p_i is greater than or equal to top_p. Lastly, a sample
    //   is drawn from the resulting distribution.
    static absl::StatusOr<std::unique_ptr<Sampler>> Create(Type type, int top_k,
                                                           float top_p,
                                                           float temperature,
                                                           int seed);
    // Given an input tensor of shape `(Batch, 1 [seq_len], vocab_size)`, runs
    // the configured sampling algorithm to find a winning class. The results are
    // reported as a vector of integer indicies where each entry corresponds to a
    // batch.
    absl::StatusOr<std::vector<int>> Sample(Halide::Runtime::Buffer<float> &logits);

private:
    Sampler(Type type, int top_k, float top_p, float temperature, int seed);
    absl::StatusOr<std::vector<int>> SampleGreedy(Halide::Runtime::Buffer<float> &logits);
    absl::StatusOr<std::vector<int>> SampleTopK(Halide::Runtime::Buffer<float> &logits);
    absl::StatusOr<std::vector<int>> SampleTopP(Halide::Runtime::Buffer<float> &logits);
    absl::Status SelectTopK(std::vector<std::pair<float, int>> &logits_ids, int k);
    // `logits_ids` must be sorted and normalized.
    absl::Status SelectTopP(std::vector<std::pair<float, int>> &logits_ids, float p);
    // `logits_ids` must be sorted.
    absl::Status ScaledSoftmax(std::vector<std::pair<float, int>> &logits_ids, bool normalize);
    absl::StatusOr<int> DoSampling(std::vector<std::pair<float, int>> &logits_ids);

    const Type type_;
    const int top_k_;
    const float top_p_;
    const float temperature_;
    std::unique_ptr<std::mt19937> generator_;
};

}  // namespace hallmark

#endif  // HALIDE_APPS_HALLMARK_SAMPLER_H_
