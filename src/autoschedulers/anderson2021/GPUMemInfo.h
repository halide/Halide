#ifndef GPU_MEM_INFO_H
#define GPU_MEM_INFO_H

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ASLog.h"
#include "Errors.h"

/** \file
 *
 * Data structures that help track memory access information. Useful when
 * computing GPU features
 */

namespace Halide {
namespace Internal {
namespace Autoscheduler {

struct GlobalMem;
struct GlobalAccessAccumulator;
struct SharedMem;
struct SharedAccessAccumulator;
struct LocalMem;
struct LocalAccessAccumulator;

template<typename T>
struct MemTraits;

template<>
struct MemTraits<GlobalMem> {
    static constexpr double bytes_per_transaction = 32;
    using MemInfoType = GlobalMem;
    using Accumulator = GlobalAccessAccumulator;
};

template<>
struct MemTraits<SharedMem> {
    static constexpr double bytes_per_transaction = 128;
    using MemInfoType = SharedMem;
    using Accumulator = SharedAccessAccumulator;
};

template<>
struct MemTraits<LocalMem> {
    static constexpr double bytes_per_transaction = 32;
    using MemInfoType = GlobalMem;  // Local mem behaves similarly to global mem
    using Accumulator = LocalAccessAccumulator;
};

template<typename T>
using Accumulator = typename MemTraits<T>::Accumulator;

template<typename T>
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

    void add(const MemInfo<T> &other) {
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

template<typename T>
using MemInfoType = MemInfo<typename MemTraits<T>::MemInfoType>;

using GlobalMemInfo = MemInfoType<GlobalMem>;
using SharedMemInfo = MemInfoType<SharedMem>;
using LocalMemInfo = MemInfoType<LocalMem>;

struct Strides {
public:
    explicit Strides(const std::vector<int64_t> &storage_strides)
        : storage_strides{storage_strides} {
    }

    void add_valid(const std::vector<double> &strides) {
        add(strides, true);
    }

    void add_invalid() {
        add({}, false);
    }

    bool valid(size_t loop_index) const {
        return is_valid[loop_index];
    }

    int64_t offset(size_t loop_index, int64_t point) const {
        internal_assert(loop_index < is_valid.size() && valid(loop_index));
        internal_assert(index_strides[loop_index].size() == storage_strides.size());

        int64_t result = 0;
        for (size_t i = 0; i < storage_strides.size(); ++i) {
            result += (int64_t)(point * index_strides[loop_index][i]) * storage_strides[i];
        }
        return std::abs(result);
    }

    void dump(bool verbose = false) {
        if (!verbose) {
            return;
        }

        for (size_t i = 0; i < storage_strides.size(); ++i) {
            if (!valid(i)) {
                aslog(2) << "stride " << i << ": invalid\n";
                continue;
            }
            aslog(2) << "storage_stride " << i << ": " << storage_strides[i] << "\n";
        }

        for (size_t i = 0; i < index_strides.size(); ++i) {
            for (size_t j = 0; j < index_strides[i].size(); ++j) {
                aslog(2) << "index_stride " << i << ", storage_stride " << j << ": " << index_strides[i][j] << " ";
            }
            aslog(2) << "\n";
        }
    }

private:
    void add(const std::vector<double> &strides, bool e) {
        index_strides.push_back(strides);
        is_valid.push_back(e);
    }

    std::vector<int64_t> storage_strides;
    std::vector<std::vector<double>> index_strides;
    std::vector<bool> is_valid;
};

struct GlobalAccessAccumulator {
    GlobalAccessAccumulator(int bytes_per_access, size_t dimensions, const Strides &strides, bool verbose)
        : bytes_per_access{bytes_per_access}, dimensions{dimensions}, strides{strides}, verbose{verbose} {
    }

    void operator()(int thread_id, int x, int y, int z, int active, bool last_thread) {
        if (!active) {
            return;
        }

        if (verbose) {
            aslog(2) << "thread_id: " << thread_id << " (" << x << ", " << y << ", " << z << ")\n";
        }

        int thread_ids[3] = {x, y, z};
        int64_t byte = 0;
        for (size_t i = 0; i < dimensions; ++i) {
            if (!strides.valid(i)) {
                ++unknown_sectors;
                return;
            }
            byte += bytes_per_access * strides.offset(i, thread_ids[i]);
        }

        if (verbose) {
            aslog(2) << "byte accessed: " << byte << "\n";
        }

        int64_t sector = byte / 32;
        if (verbose) {
            aslog(2) << "sectors accessed: ";
        }
        for (int i = 0; i < bytes_per_access; ++i) {
            if (verbose) {
                aslog(2) << sector << " ";
            }
            sectors_accessed[sector].insert(byte + i);
        }
        if (verbose) {
            aslog(2) << "\n\n";
        }
    }

    void add_access_info(int num_requests, GlobalMemInfo &global_mem_info, bool is_tail_warp) const {
        int num_transactions_per_request = sectors_accessed.size() + unknown_sectors;

        if (verbose) {
            if (is_tail_warp) {
                aslog(2) << "tail_";
            }
            aslog(2) << "num_transactions_per_request = " << num_transactions_per_request << "\n";
        }

        int num_bytes_used_per_request = 0;
        for (const auto &sector : sectors_accessed) {
            num_bytes_used_per_request += sector.second.size();
        }

        num_bytes_used_per_request += unknown_sectors * bytes_per_access;

        if (verbose) {
            if (is_tail_warp) {
                aslog(2) << "tail_";
            }
            aslog(2) << "num_requests_per_block = " << num_requests << "\n";
        }

        global_mem_info.add_access_info(
            num_requests,
            num_transactions_per_request,
            num_bytes_used_per_request);
    }

private:
    int bytes_per_access;
    size_t dimensions;
    Strides strides;
    bool verbose;
    int unknown_sectors = 0;
    std::unordered_map<int64_t, std::unordered_set<int64_t>> sectors_accessed;
};

struct SharedAccessAccumulator {
    SharedAccessAccumulator(int bytes_per_access, size_t dimensions, const Strides &strides, bool verbose)
        : bytes_per_access{bytes_per_access}, dimensions{dimensions}, strides{strides}, verbose{verbose} {
    }

    void operator()(int thread_id, int x, int y, int z, int active, bool last_thread) {
        if (!active) {
            return;
        }

        if (verbose) {
            aslog(2) << "thread_id: " << thread_id << " (" << x << ", " << y << ", " << z << ")\n";
        }

        int thread_ids[3] = {x, y, z};
        int64_t byte = 0;
        for (size_t i = 0; i < dimensions; ++i) {
            if (!strides.valid(i)) {
                ++unknown_banks;
                return;
            }
            byte += bytes_per_access * strides.offset(i, thread_ids[i]);
        }

        if (verbose) {
            aslog(2) << "bytes accessed: ";
            for (int i = 0; i < bytes_per_access; ++i) {
                aslog(2) << byte + i << " ";
            }
            aslog(2) << "\n";
        }

        if (verbose) {
            aslog(2) << "banks accessed: ";
        }
        for (int i = 0; i < bytes_per_access; ++i) {
            int64_t word = (byte + i) / 4;
            int64_t bank = word % 32;
            if (verbose) {
                aslog(2) << bank << " ";
            }
            bytes_accessed.insert(byte + i);
            bank_to_words_accessed[bank].insert(word);
        }
        if (verbose) {
            aslog(2) << "\n\n";
        }
    }

    void add_access_info(int num_requests, SharedMemInfo &shared_mem_info, bool is_tail_warp) const {
        int num_transactions_per_request = 0;
        for (const auto &bank : bank_to_words_accessed) {
            num_transactions_per_request = std::max(num_transactions_per_request, (int)bank.size());
        }

        num_transactions_per_request += unknown_banks;

        if (verbose) {
            if (is_tail_warp) {
                aslog(2) << "tail_";
            }
            aslog(2) << "num_transactions_per_request = " << num_transactions_per_request << "\n";
        }

        int num_bytes_used_per_request = bytes_accessed.size();

        num_bytes_used_per_request += unknown_banks * bytes_per_access;

        if (verbose) {
            if (is_tail_warp) {
                aslog(2) << "tail_";
            }
            aslog(2) << "num_requests_per_block = " << num_requests << "\n";
        }

        shared_mem_info.add_access_info(
            num_requests,
            num_transactions_per_request,
            num_bytes_used_per_request);
    }

private:
    int bytes_per_access;
    size_t dimensions;
    Strides strides;
    bool verbose;
    int unknown_banks = 0;
    std::unordered_set<int64_t> bytes_accessed;
    std::array<std::unordered_set<int64_t>, 32> bank_to_words_accessed;
};

struct LocalAccessAccumulator {
    LocalAccessAccumulator(int bytes_per_access, bool verbose)
        : bytes_per_access{bytes_per_access}, verbose{verbose} {
    }

    void operator()(int thread_id, int x, int y, int z, int active, bool last_thread) {
        if (!active) {
            return;
        }

        ++thread_count;

        if (verbose) {
            aslog(2) << "thread_id: " << thread_id << " (" << x << ", " << y << ", " << z << ")\n";
        }
    }

    void add_access_info(int num_requests, LocalMemInfo &local_mem_info, bool is_tail_warp) const {
        int num_bytes_used_per_request = thread_count * bytes_per_access;
        int sectors_accessed = std::ceil((float)num_bytes_used_per_request / (float)LocalMemInfo::bytes_per_transaction);
        int num_transactions_per_request = sectors_accessed;

        if (verbose) {
            if (is_tail_warp) {
                aslog(2) << "tail_";
            }
            aslog(2) << "num_transactions_per_request = " << num_transactions_per_request << "\n";
        }

        if (verbose) {
            if (is_tail_warp) {
                aslog(2) << "tail_";
            }
            aslog(2) << "num_requests_per_block = " << num_requests << "\n";
        }

        local_mem_info.add_access_info(
            num_requests,
            num_transactions_per_request,
            num_bytes_used_per_request);
    }

private:
    int bytes_per_access;
    bool verbose;
    int thread_count = 0;
    std::unordered_map<int64_t, std::unordered_set<int64_t>> sectors_accessed;
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif  // GPU_MEM_INFO_H
