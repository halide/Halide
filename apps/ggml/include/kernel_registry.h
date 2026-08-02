#pragma once

// Implementation-agnostic core of kernel-bench.
//
// A "kernel" here is identified by a ggml_type plus a category (quantize,
// dequantize, vec_dot, repack quantize_mat/gemv/gemm). For each (category,
// type) pair, exactly one *reference* implementation may be registered --
// this is the correctness ground truth and baseline timing that every other
// *candidate* implementation for that (category, type) is measured against.
//
// GGML's own routines are registered as the reference by providers/ggml_provider.cpp.
// Nothing in this file knows anything about GGML internals: a future provider
// (e.g. the user's own reference implementation) is just another call to
// register_candidate() (or register_reference(), if it should replace GGML's
// as the ground truth) with a function pointer matching the category's
// signature. See providers/README.md for how to add one.

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <ggml.h>

// Function-pointer shapes shared by every provider. These mirror the layouts
// used throughout ggml-cpu (quantize_row_*, ggml_vec_dot_*, ggml_gemv_*/ggml_gemm_*)
// so that both GGML's own symbols and a from-scratch implementation can be
// registered without adapters.
using quantize_fn_t = void (*)(const float *GGML_RESTRICT x, void *GGML_RESTRICT y, int64_t k);
using dequantize_fn_t = void (*)(const void *GGML_RESTRICT x, float *GGML_RESTRICT y, int64_t k);
using vec_dot_fn_t = void (*)(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx, size_t bx,
                              const void *GGML_RESTRICT vy, size_t by, int nrc);
using gemx_fn_t = void (*)(int n, float *GGML_RESTRICT s, size_t bs, const void *GGML_RESTRICT vx,
                           const void *GGML_RESTRICT vy, int nr, int nc);

template<typename Fn>
struct Impl {
    std::string name;
    Fn fn;
};

template<typename Fn>
class Registry {
public:
    // Exactly one reference per type. Calling this twice for the same type
    // replaces the previous reference (last call wins) -- useful if a later
    // provider should become the new ground truth for that type.
    void register_reference(ggml_type type, std::string name, Fn fn) {
        entries_[type].reference = Impl<Fn>{std::move(name), fn};
    }

    // Zero or more per type.
    void register_candidate(ggml_type type, std::string name, Fn fn) {
        entries_[type].candidates.push_back(Impl<Fn>{std::move(name), fn});
    }

    const Impl<Fn> *reference(ggml_type type) const {
        auto it = entries_.find(type);
        if (it == entries_.end() || !it->second.reference.has_value()) {
            return nullptr;
        }
        return &*it->second.reference;
    }

    const std::vector<Impl<Fn>> &candidates(ggml_type type) const {
        static const std::vector<Impl<Fn>> empty;
        auto it = entries_.find(type);
        return it == entries_.end() ? empty : it->second.candidates;
    }

    std::vector<ggml_type> types_with_reference() const {
        std::vector<ggml_type> out;
        for (const auto &[type, entry] : entries_) {
            if (entry.reference.has_value()) {
                out.push_back(type);
            }
        }
        return out;
    }

private:
    struct Entry {
        std::optional<Impl<Fn>> reference;
        std::vector<Impl<Fn>> candidates;
    };
    std::map<ggml_type, Entry> entries_;
};

// Repack kernels are additionally keyed by the activation (vec_dot_type)
// they were interleaved against, and by the interleave geometry -- carried
// alongside the ggml_type key as a small identifying suffix (e.g. "4x4",
// "8x8") so multiple repack variants can coexist for the same base type.
struct RepackKey {
    ggml_type base_type;
    ggml_type act_type;
    int inter_size;
    int nb_cols;
    std::string label;  // e.g. "q4_0_4x4_q8_0", used for display and as a stable map key

    bool operator<(const RepackKey &other) const {
        return label < other.label;
    }
};

template<typename Fn>
class RepackRegistry {
public:
    void register_reference(const RepackKey &key, std::string name, Fn fn) {
        entries_[key.label].key = key;
        entries_[key.label].reference = Impl<Fn>{std::move(name), fn};
    }
    void register_candidate(const RepackKey &key, std::string name, Fn fn) {
        entries_[key.label].key = key;
        entries_[key.label].candidates.push_back(Impl<Fn>{std::move(name), fn});
    }
    const Impl<Fn> *reference(const std::string &label) const {
        auto it = entries_.find(label);
        if (it == entries_.end() || !it->second.reference.has_value()) {
            return nullptr;
        }
        return &*it->second.reference;
    }
    const std::vector<Impl<Fn>> &candidates(const std::string &label) const {
        static const std::vector<Impl<Fn>> empty;
        auto it = entries_.find(label);
        return it == entries_.end() ? empty : it->second.candidates;
    }
    std::vector<RepackKey> keys() const {
        std::vector<RepackKey> out;
        for (const auto &[label, entry] : entries_) {
            if (entry.reference.has_value()) {
                out.push_back(entry.key);
            }
        }
        return out;
    }

private:
    struct Entry {
        RepackKey key{};
        std::optional<Impl<Fn>> reference;
        std::vector<Impl<Fn>> candidates;
    };
    std::map<std::string, Entry> entries_;
};

struct KernelRegistries {
    Registry<quantize_fn_t> quantize;
    Registry<dequantize_fn_t> dequantize;
    Registry<vec_dot_fn_t> vec_dot;
    RepackRegistry<quantize_fn_t> repack_quantize_mat;
    RepackRegistry<gemx_fn_t> repack_gemv;
    RepackRegistry<gemx_fn_t> repack_gemm;
};
