#include "Approximation.h"

#include "Error.h"

namespace Halide {

EncodeResult Compose::encode(std::vector<Func> inputs) {
    EncodeResult inner_result = inner_.encode(std::move(inputs));
    EncodeResult outer_result = outer_.encode(inner_result.encoded);

    std::vector<Func> handles = inner_result.encoded;
    handles.insert(handles.end(), inner_result.handles.begin(), inner_result.handles.end());
    handles.insert(handles.end(), outer_result.handles.begin(), outer_result.handles.end());
    return {outer_result.encoded, handles};
}

DecodeResult Compose::decode(std::vector<Func> encoded) {
    DecodeResult outer_result = outer_.decode(std::move(encoded));
    DecodeResult inner_result = inner_.decode(outer_result.decoded);

    std::vector<Func> handles = outer_result.decoded;
    handles.insert(handles.end(), outer_result.handles.begin(), outer_result.handles.end());
    handles.insert(handles.end(), inner_result.handles.begin(), inner_result.handles.end());
    return {inner_result.decoded, handles};
}

EncodeResult Apply::encode(std::vector<Func> inputs) {
    user_assert(idx_ + encode_arity_ <= (int)inputs.size())
        << "Apply::encode: idx (" << idx_ << ") + encode_arity (" << encode_arity_
        << ") exceeds the input count (" << inputs.size() << ")\n";
    std::vector<Func> target(inputs.begin() + idx_, inputs.begin() + idx_ + encode_arity_);
    EncodeResult inner_result = inner_.encode(std::move(target));

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
    DecodeResult inner_result = inner_.decode(std::move(target));

    std::vector<Func> decoded(encoded.begin(), encoded.begin() + idx_);
    decoded.insert(decoded.end(), inner_result.decoded.begin(), inner_result.decoded.end());
    decoded.insert(decoded.end(), encoded.begin() + idx_ + decode_arity_, encoded.end());
    return {decoded, inner_result.handles};
}

}  // namespace Halide
