#include "Approximation.h"

#include "Error.h"

namespace Halide {

namespace {

Func find_stage_output(const std::vector<ApproximationStageOutputs> &outputs,
                       const ApproximationStageKey &stage, size_t port) {
    if (!stage.defined()) {
        return Func();
    }
    for (auto it = outputs.rbegin(); it != outputs.rend(); ++it) {
        if (it->stage == stage) {
            return port < it->ports.size() ? it->ports[port] : Func();
        }
    }
    return Func();
}

}  // namespace

Func ApproximationResult::encoded_by(const ApproximationStageKey &stage, size_t port) const {
    return find_stage_output(encoded_stage_outputs, stage, port);
}

Func ApproximationResult::decoded_by(const ApproximationStageKey &stage, size_t port) const {
    return find_stage_output(decoded_stage_outputs, stage, port);
}

EncodeResult Compose::encode(std::vector<Func> inputs) {
    user_assert(!stages_.empty()) << "Compose::encode: no stages\n";

    std::vector<Func> handles;
    std::vector<ApproximationStageOutputs> stage_outputs;
    std::vector<Func> current = std::move(inputs);
    for (int i = (int)stages_.size() - 1; i >= 0; i--) {
        EncodeResult r = stages_[i]->encode(std::move(current));
        stage_outputs.insert(stage_outputs.end(), r.stage_outputs.begin(), r.stage_outputs.end());
        stage_outputs.push_back({stages_[i]->stage_key(), r.encoded});
        if (i > 0) {
            // Not the final (outermost) stage -- its encoded output is an
            // intermediate between stages, so it needs scheduling like any
            // other handle, but isn't part of the signature contract this
            // Compose itself returns.
            handles.insert(handles.end(), r.encoded.begin(), r.encoded.end());
        }
        handles.insert(handles.end(), r.handles.begin(), r.handles.end());
        current = std::move(r.encoded);
    }
    return {current, handles, stage_outputs};
}

DecodeResult Compose::decode(std::vector<Func> encoded) {
    user_assert(!stages_.empty()) << "Compose::decode: no stages\n";

    std::vector<Func> handles;
    std::vector<ApproximationStageOutputs> stage_outputs;
    std::vector<Func> current = std::move(encoded);
    for (int i = 0; i < (int)stages_.size(); i++) {
        DecodeResult r = stages_[i]->decode(std::move(current));
        stage_outputs.insert(stage_outputs.end(), r.stage_outputs.begin(), r.stage_outputs.end());
        stage_outputs.push_back({stages_[i]->stage_key(), r.decoded});
        if (i + 1 < (int)stages_.size()) {
            handles.insert(handles.end(), r.decoded.begin(), r.decoded.end());
        }
        handles.insert(handles.end(), r.handles.begin(), r.handles.end());
        current = std::move(r.decoded);
    }
    return {current, handles, stage_outputs};
}

EncodeResult Apply::encode(std::vector<Func> inputs) {
    user_assert(idx_ + encode_arity_ <= (int)inputs.size())
        << "Apply::encode: idx (" << idx_ << ") + encode_arity (" << encode_arity_
        << ") exceeds the input count (" << inputs.size() << ")\n";
    std::vector<Func> target(inputs.begin() + idx_, inputs.begin() + idx_ + encode_arity_);
    EncodeResult inner_result = inner_->encode(std::move(target));

    std::vector<Func> encoded(inputs.begin(), inputs.begin() + idx_);
    encoded.insert(encoded.end(), inner_result.encoded.begin(), inner_result.encoded.end());
    encoded.insert(encoded.end(), inputs.begin() + idx_ + encode_arity_, inputs.end());
    inner_result.stage_outputs.push_back({inner_->stage_key(), inner_result.encoded});
    return {encoded, inner_result.handles, inner_result.stage_outputs};
}

DecodeResult Apply::decode(std::vector<Func> encoded) {
    user_assert(idx_ + decode_arity_ <= (int)encoded.size())
        << "Apply::decode: idx (" << idx_ << ") + decode_arity (" << decode_arity_
        << ") exceeds the input count (" << encoded.size() << ")\n";
    std::vector<Func> target(encoded.begin() + idx_, encoded.begin() + idx_ + decode_arity_);
    DecodeResult inner_result = inner_->decode(std::move(target));

    std::vector<Func> decoded(encoded.begin(), encoded.begin() + idx_);
    decoded.insert(decoded.end(), inner_result.decoded.begin(), inner_result.decoded.end());
    decoded.insert(decoded.end(), encoded.begin() + idx_ + decode_arity_, encoded.end());
    inner_result.stage_outputs.push_back({inner_->stage_key(), inner_result.decoded});
    return {decoded, inner_result.handles, inner_result.stage_outputs};
}

EncodeResult TrustedInverse::encode(std::vector<Func> inputs) {
    EncodeResult r = encoder_->encode(std::move(inputs));
    r.stage_outputs.push_back({encoder_->stage_key(), r.encoded});
    return r;
}

DecodeResult TrustedInverse::decode(std::vector<Func> encoded) {
    DecodeResult r = decoder_->decode(std::move(encoded));
    r.stage_outputs.push_back({decoder_->stage_key(), r.decoded});
    return r;
}

}  // namespace Halide
