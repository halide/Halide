#include "Approximation.h"

#include "Error.h"

namespace Halide {

EncodeResult Compose::encode(std::vector<Func> inputs) {
    user_assert(!stages_.empty()) << "Compose::encode: no stages\n";

    std::vector<Func> handles;
    std::vector<Func> current = std::move(inputs);
    for (int i = (int)stages_.size() - 1; i >= 0; i--) {
        EncodeResult r = stages_[i]->encode(std::move(current));
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
    return {current, handles};
}

DecodeResult Compose::decode(std::vector<Func> encoded) {
    user_assert(!stages_.empty()) << "Compose::decode: no stages\n";

    std::vector<Func> handles;
    std::vector<Func> current = std::move(encoded);
    for (int i = 0; i < (int)stages_.size(); i++) {
        DecodeResult r = stages_[i]->decode(std::move(current));
        if (i + 1 < (int)stages_.size()) {
            handles.insert(handles.end(), r.decoded.begin(), r.decoded.end());
        }
        handles.insert(handles.end(), r.handles.begin(), r.handles.end());
        current = std::move(r.decoded);
    }
    return {current, handles};
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
    return {encoded, inner_result.handles};
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
    return {decoded, inner_result.handles};
}

EncodeResult TrustedInverse::encode(std::vector<Func> inputs) {
    return encoder_->encode(std::move(inputs));
}

DecodeResult TrustedInverse::decode(std::vector<Func> encoded) {
    return decoder_->decode(std::move(encoded));
}

}  // namespace Halide
