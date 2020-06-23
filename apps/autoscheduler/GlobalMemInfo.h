#ifndef GPU_MEM_INFO_H
#define GPU_MEM_INFO_H

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ASLog.h"

/** \file
 *
 * Data structure that helps track global memory access information. Useful when 
 * computing GPU features
 */

namespace Halide {
namespace Internal {
namespace Autoscheduler {

struct GlobalMem;
struct GlobalAccessAccumulator;
struct SharedMem;
struct SharedAccessAccumulator;

template <typename T>
struct MemTraits;

template <>
struct MemTraits<GlobalMem> {
    static constexpr double bytes_per_transaction = 32;
    using Accumulator = GlobalAccessAccumulator;
};

template <>
struct MemTraits<SharedMem> {
    static constexpr double bytes_per_transaction = 128;
    using Accumulator = SharedAccessAccumulator;
};

template <typename T>
using Accumulator = typename MemTraits<T>::Accumulator;

template <typename T>
struct MemInfo {
    static constexpr double bytes_per_transaction = MemTraits<T>::bytes_per_transaction;

    double num_transactions() const {
        return total_num_transactions;
    }

    void add_access_info(double num_requests, double num_transactions_per_request, double num_bytes_used_per_request) {
        internal_assert(num_bytes_used_per_request > 0);

        double total_transactions = num_requests * num_transactions_per_request;
        double total_bytes = total_transactions * bytes_per_transaction;
        double total_bytes_used = num_requests * num_bytes_used_per_request;

        internal_assert(total_bytes_used <= total_bytes) 
            << "\ntotal_bytes_used = " << total_bytes_used
            << "\ntotal_bytes = " << total_bytes
            << "\ntotal_transactions = " << total_transactions
            << "\nnum_transactions_per_request = " << num_transactions_per_request
            << "\nnum_requests = " << num_requests;

        update_totals(total_transactions, total_bytes_used, total_bytes);
    }

    void add(const MemInfo<T>& other) {
        total_num_transactions += other.total_num_transactions;
        total_num_bytes_used += other.total_num_bytes_used;
        total_num_bytes += other.total_num_bytes;
    }

    double efficiency() const {
        if (total_num_bytes == 0) {
            return 1;
        }

        double result = total_num_bytes_used / total_num_bytes;
        internal_assert(result <= 1);
        return result;
    }

private:
    void update_totals(double num_transactions, double num_bytes_used, double num_bytes) {
        total_num_transactions += num_transactions;
        total_num_bytes_used += num_bytes_used;
        total_num_bytes += num_bytes;
    }

    double total_num_transactions = 0;
    double total_num_bytes_used = 0;
    double total_num_bytes = 0;
};

using GlobalMemInfo = MemInfo<GlobalMem>;
using SharedMemInfo = MemInfo<SharedMem>;

struct StorageStrides {
public:
    void add_valid(double stride) {
        add(stride, true);
    }

    void add_invalid() {
        add(0, false);
    }

    void multiply_by_scalar(double scalar) {
        for (double& s : values) {
            s *= scalar;
        }
    }

    bool valid(size_t i) const {
        return is_valid[i];
    }

    double operator[](size_t i) const {
        return values[i];
    }
private:
    void add(double stride, bool e) {
        values.push_back(stride);
        is_valid.push_back(e);
    }
    std::vector<double> values;
    std::vector<bool> is_valid;
};

struct GlobalAccessAccumulator {
    GlobalAccessAccumulator(int bytes_per_access, size_t dimensions, const StorageStrides& strides, bool verbose)
        : bytes_per_access{bytes_per_access}
        , dimensions{dimensions}
        , strides{strides}
        , verbose{verbose}
    {}

    void operator()(int thread_id, int x, int y, int z, int active, bool last_thread) {
        if (!active) {
            return;
        }

        if (verbose) {
            aslog(0) << "thread_id: " << thread_id << " (" << x << ", " << y << ", " << z << ")\n"; 
        }

        int thread_ids[3] = {x, y, z};
        int64_t byte = 0;
        for (size_t i = 0; i < dimensions; ++i) {
            if (!strides.valid(i)) {
                ++unknown_sectors;
                return;
            }
            byte += bytes_per_access * (int)(thread_ids[i] * strides[i]);
        }

        if (verbose) {
            aslog(0) << "byte accessed: " << byte << "\n";
        }

        int64_t sector = byte / 32;
        if (verbose) {
            aslog(0) << "sectors accessed: ";
        }
        for (int i = 0; i < bytes_per_access; ++i) {
            if (verbose) {
                aslog(0) << sector << " ";
            }
            sectors_accessed[sector].insert(byte + i);
        }
        if (verbose) {
            aslog(0) << "\n\n";
        }
    }

    void add_access_info(int num_requests, GlobalMemInfo& global_mem_info, bool is_tail_warp) const {
        int num_transactions_per_request = sectors_accessed.size() + unknown_sectors;

        if (verbose) {
            if (is_tail_warp) {
                aslog(0) << "tail_";
            }
            aslog(0) << "num_transactions_per_request = " << num_transactions_per_request << "\n";
        }

        int num_bytes_used_per_request = 0;
        for (const auto& sector : sectors_accessed) {
            num_bytes_used_per_request += sector.second.size();
        }

        num_bytes_used_per_request += unknown_sectors * bytes_per_access;

        if (verbose) {
            if (is_tail_warp) {
                aslog(0) << "tail_";
            }
            aslog(0) << "num_requests_per_block = " << num_requests << "\n";
        }

        global_mem_info.add_access_info(
            num_requests,
            num_transactions_per_request,
            num_bytes_used_per_request
        );
    }

private:
    int bytes_per_access;
    size_t dimensions;
    StorageStrides strides;
    bool verbose;
    int unknown_sectors = 0;
    std::unordered_map<int64_t, std::unordered_set<int64_t>> sectors_accessed;
};

struct SharedAccessAccumulator {
    SharedAccessAccumulator(int bytes_per_access, size_t dimensions, const StorageStrides& strides, bool verbose)
        : bytes_per_access{bytes_per_access}
        , dimensions{dimensions}
        , strides{strides}
        , verbose{verbose}
    {}

    void operator()(int thread_id, int x, int y, int z, int active, bool last_thread) {
        if (!active) {
            return;
        }

        if (verbose) {
            aslog(0) << "thread_id: " << thread_id << " (" << x << ", " << y << ", " << z << ")\n"; 
        }

        int thread_ids[3] = {x, y, z};
        int64_t byte = 0;
        for (size_t i = 0; i < dimensions; ++i) {
            if (!strides.valid(i)) {
                ++unknown_banks;
                return;
            }
            byte += bytes_per_access * (int)(thread_ids[i] * strides[i]);
        }

        if (verbose) {
            aslog(0) << "bytes accessed: ";
            for (int i = 0; i < bytes_per_access; ++i) {
                aslog(0) << byte + i << " ";
            }
            aslog(0) << "\n";
        }

        if (verbose) {
            aslog(0) << "banks accessed: ";
        }
        for (int i = 0; i < bytes_per_access; ++i) {
            int64_t word = (byte + i) / 4;
            int64_t bank = word % 32;
            if (verbose) {
                aslog(0) << bank << " ";
            }
            bytes_accessed.insert(byte + i);
            bank_to_words_accessed[bank].insert(word);
        }
        if (verbose) {
            aslog(0) << "\n\n";
        }
    }

    void add_access_info(int num_requests, SharedMemInfo& shared_mem_info, bool is_tail_warp) const {
        int num_transactions_per_request = 0;
        for (const auto& bank : bank_to_words_accessed) {
            num_transactions_per_request = std::max(num_transactions_per_request, (int)bank.size());
        }

        num_transactions_per_request += unknown_banks;

        if (verbose) {
            if (is_tail_warp) {
                aslog(0) << "tail_";
            }
            aslog(0) << "num_transactions_per_request = " << num_transactions_per_request << "\n";
        }

        int num_bytes_used_per_request = bytes_accessed.size();

        num_bytes_used_per_request += unknown_banks * bytes_per_access;

        if (verbose) {
            if (is_tail_warp) {
                aslog(0) << "tail_";
            }
            aslog(0) << "num_requests_per_block = " << num_requests << "\n";
        }

        shared_mem_info.add_access_info(
            num_requests,
            num_transactions_per_request,
            num_bytes_used_per_request
        );
    }

private:
    int bytes_per_access;
    size_t dimensions;
    StorageStrides strides;
    bool verbose;
    int unknown_banks = 0;
    std::unordered_set<int64_t> bytes_accessed;
    std::array<std::unordered_set<int64_t>, 32> bank_to_words_accessed;
};

struct LocalMemInfo {
    void add_access(double num_accesses, double stride) {
        total_accesses += num_accesses;
        add_stride(stride);
    }

    double average_efficiency() const {
        if (total_stride == 0) {
            return 1.0;
        }
        return 1.0 / (total_stride / num_entries);
    }

    double total_accesses = 0;

private:
    void add_stride(double stride) {
        if (stride == 0) {
            return;
        }

        total_stride += std::min(32.0, std::max(1.0, stride));
        ++num_entries;
    }

    int num_entries = 0;
    double total_stride = 0;
};


}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif // GPU_MEM_INFO_H
