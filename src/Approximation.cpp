#include "Approximation.h"

#include "Error.h"

namespace Halide {

namespace {

Func find_stage_output(const std::vector<ApproximationStageOutputs> &outputs,
                       const Approximation &stage, size_t port, const char *direction) {
    user_assert(stage.defined()) << "Approximation::" << direction << "_by: undefined stage\n";
    const ApproximationStageOutputs *found = nullptr;
    size_t count = 0;
    for (const ApproximationStageOutputs &o : outputs) {
        if (o.stage.same_as(stage)) {
            found = &o;
            count++;
        }
    }
    user_assert(count > 0)
        << "Approximation::" << direction << "_by: the stage was not invoked in the "
        << direction << " direction (was it converted separately from the handle passed "
        << "into the Approximation?)\n";
    user_assert(count == 1)
        << "Approximation::" << direction << "_by: ambiguous: stage invoked " << count
        << " times in the " << direction << " direction\n";
    return port < found->ports.size() ? found->ports[port] : Func();
}

}  // namespace

Func ApproximationResult::encoded_by(const Approximation &stage, size_t port) const {
    return find_stage_output(encoded_stage_outputs, stage, port, "encode");
}

Func ApproximationResult::decoded_by(const Approximation &stage, size_t port) const {
    return find_stage_output(decoded_stage_outputs, stage, port, "decode");
}

EncodeResult Approximation::encode(const std::vector<Func> &inputs) const {
    user_assert(defined()) << "encode called on an undefined Approximation\n";
    EncodeResult r = impl_->encode(inputs);
    r.stage_outputs.push_back({*this, r.encoded});
    return r;
}

DecodeResult Approximation::decode(const std::vector<Func> &encoded) const {
    user_assert(defined()) << "decode called on an undefined Approximation\n";
    DecodeResult r = impl_->decode(encoded);
    r.stage_outputs.push_back({*this, r.decoded});
    return r;
}

EncodeResult Compose::encode(std::vector<Func> inputs) const {
    user_assert(!stages.empty()) << "Compose::encode: no stages\n";

    std::vector<Func> handles;
    std::vector<ApproximationStageOutputs> stage_outputs;
    std::vector<Func> current = std::move(inputs);
    for (int i = (int)stages.size() - 1; i >= 0; i--) {
        EncodeResult r = stages[i].encode(current);
        stage_outputs.insert(stage_outputs.end(), r.stage_outputs.begin(), r.stage_outputs.end());
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

DecodeResult Compose::decode(std::vector<Func> encoded) const {
    user_assert(!stages.empty()) << "Compose::decode: no stages\n";

    std::vector<Func> handles;
    std::vector<ApproximationStageOutputs> stage_outputs;
    std::vector<Func> current = std::move(encoded);
    for (int i = 0; i < (int)stages.size(); i++) {
        DecodeResult r = stages[i].decode(current);
        stage_outputs.insert(stage_outputs.end(), r.stage_outputs.begin(), r.stage_outputs.end());
        if (i + 1 < (int)stages.size()) {
            handles.insert(handles.end(), r.decoded.begin(), r.decoded.end());
        }
        handles.insert(handles.end(), r.handles.begin(), r.handles.end());
        current = std::move(r.decoded);
    }
    return {current, handles, stage_outputs};
}

EncodeResult Apply::encode(std::vector<Func> inputs) const {
    user_assert(idx + encode_arity <= (int)inputs.size())
        << "Apply::encode: idx (" << idx << ") + encode_arity (" << encode_arity
        << ") exceeds the input count (" << inputs.size() << ")\n";
    std::vector<Func> target(inputs.begin() + idx, inputs.begin() + idx + encode_arity);
    EncodeResult inner_result = inner.encode(target);

    std::vector<Func> encoded(inputs.begin(), inputs.begin() + idx);
    encoded.insert(encoded.end(), inner_result.encoded.begin(), inner_result.encoded.end());
    encoded.insert(encoded.end(), inputs.begin() + idx + encode_arity, inputs.end());
    return {encoded, inner_result.handles, inner_result.stage_outputs};
}

DecodeResult Apply::decode(std::vector<Func> encoded) const {
    user_assert(idx + decode_arity <= (int)encoded.size())
        << "Apply::decode: idx (" << idx << ") + decode_arity (" << decode_arity
        << ") exceeds the input count (" << encoded.size() << ")\n";
    std::vector<Func> target(encoded.begin() + idx, encoded.begin() + idx + decode_arity);
    DecodeResult inner_result = inner.decode(target);

    std::vector<Func> decoded(encoded.begin(), encoded.begin() + idx);
    decoded.insert(decoded.end(), inner_result.decoded.begin(), inner_result.decoded.end());
    decoded.insert(decoded.end(), encoded.begin() + idx + decode_arity, encoded.end());
    return {decoded, inner_result.handles, inner_result.stage_outputs};
}

EncodeResult TrustedInverse::encode(std::vector<Func> inputs) const {
    return encoder.encode(inputs);
}

DecodeResult TrustedInverse::decode(std::vector<Func> encoded) const {
    return decoder.decode(encoded);
}

EncodeResult Choose::encode(std::vector<Func> inputs) const {
    return chosen.encode(inputs);
}

DecodeResult Choose::decode(std::vector<Func> encoded) const {
    return chosen.decode(encoded);
}

EncodeResult Identity::encode(std::vector<Func> inputs) const {
    return {inputs, {}, {}};
}

DecodeResult Identity::decode(std::vector<Func> encoded) const {
    return {encoded, {}, {}};
}

EncodeResult Permute::encode(std::vector<Func> inputs) const {
    user_assert(inputs.size() == forward.size()) << "Permutation size does not match input size";
    std::vector<Func> result;
    result.reserve(inputs.size());
    for (int i = 0; i < (int)inputs.size(); i++) {
        result.push_back(inputs[forward[i]]);
    }
    return {result, {}, {}};
}

DecodeResult Permute::decode(std::vector<Func> encoded) const {
    user_assert(encoded.size() == forward.size()) << "Permutation size does not match encoded size";
    std::vector<Func> result;
    result.reserve(encoded.size());
    for (int i = 0; i < (int)encoded.size(); i++) {
        result.push_back(encoded[backward[i]]);
    }
    return {result, {}, {}};
}

}  // namespace Halide
