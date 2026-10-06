#include "harness.h"

namespace gq {
namespace {

// Linear layers (K -> N) of real models; every K and N is a multiple of 256
// so all block sizes apply.
struct Layer {
    const char *model, *layer;
    int64_t K, N;
};
const Layer layers[] = {
    {"llama3-8b", "attn_q", 4096, 4096},  // = attn_output
    {"llama3-8b", "attn_kv", 4096, 1024},
    {"llama3-8b", "ffn_up", 4096, 14336},  // = ffn_gate
    {"llama3-8b", "ffn_down", 14336, 4096},
    {"llama3.2-1b", "attn_q", 2048, 2048},
    {"llama3.2-1b", "attn_kv", 2048, 512},
    {"llama3.2-1b", "ffn_up", 2048, 8192},
    {"llama3.2-1b", "ffn_down", 8192, 2048},
    {"qwen2.5-7b", "attn_q", 3584, 3584},
    {"qwen2.5-7b", "attn_kv", 3584, 512},
    {"qwen2.5-7b", "ffn_up", 3584, 18944},
    {"qwen2.5-7b", "ffn_down", 18944, 3584},
};

}  // namespace

// Presets:
//   smoke: small shapes for correctness (and ctest)
//   gemv:  every layer at M = 1 (token generation)
//   full:  every layer at M in {1, 8, 32, 512} (decode, small batch, prefill)
//   llama3-8b-gemv / -full: just that model
//   KxNxM: one explicit shape
// vec_dot: n in {256, 4096, 14336} (smoke: 256, 4096) or an explicit K.
std::vector<Shape> shapes(const std::string &preset, int op) {
    std::vector<Shape> v;
    if (op == GQ_vec_dot) {
        if (preset == "smoke") return {{"", "", 256, 1, 1}, {"", "", 4096, 1, 1}};
        if (preset.find_first_not_of("0123456789") == std::string::npos) return {{"", "", std::stoll(preset), 1, 1}};
        for (int64_t n : {256, 4096, 14336})
            v.push_back({"", "", n, 1, 1});
        return v;
    }
    if (preset == "smoke") return {{"", "", 256, 64, 1}, {"", "", 512, 96, 5}, {"", "", 1024, 256, 40}};
    long long K, N, M;
    if (sscanf(preset.c_str(), "%lldx%lldx%lld", &K, &N, &M) == 3) return {{"", "", K, N, M}};
    std::string model = preset.substr(0, preset.rfind('-'));
    bool full = preset.size() >= 4 && preset.substr(preset.size() - 4) == "full";
    for (const Layer &l : layers) {
        if (model != "gemv" && model != "full" && model != l.model) continue;
        for (int64_t M : {1, 8, 32, 512}) {
            if (M == 1 || full) v.push_back({l.model, l.layer, l.K, l.N, M});
        }
    }
    return v;
}

}  // namespace gq
