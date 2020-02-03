#ifndef PERFECT_HASH_MAP_H
#define PERFECT_HASH_MAP_H

#include <algorithm>
#include <iostream>
#include <vector>

// Avoid a dependence on libHalide by defining a local variant we can use
struct PerfectHashMapAsserter {
    const bool c;

    PerfectHashMapAsserter(bool c)
        : c(c) {
    }

    template<typename T>
    PerfectHashMapAsserter &operator<<(T &&t) {
        if (!c) {
            std::cerr << t;
        }
        return *this;
    }
    ~PerfectHashMapAsserter() {
        if (!c) {
            exit(-1);
        }
    }
};

// A specialized hash map used in the autoscheduler. It can only grow,
// and it requires a perfect hash in the form of "id" and "max_id"
// fields on each key. If the keys don't all have a consistent max_id,
// or if you call make_large with the wrong max_id, you get UB. If you
// think that might be happening, uncomment the assertions below for
// some extra checking.

template<typename K, typename T, int max_small_size = 4, typename phm_assert = PerfectHashMapAsserter>
class PerfectHashMap {

    using storage_type = std::vector<std::pair<const K *, T>>;

    storage_type storage;

    int occupied = 0;

    // Equivalent to storage[i], but broken out into a separate method
    // to allow for bounds checks when debugging this.
    std::pair<const K *, T> &storage_bucket(int i) {
        /*
        phm_assert(i >= 0 && i < (int)storage.size())
            << "Out of bounds access: " << i << " " << storage.size() << "\n";
        */
        return storage[i];
    }

    const std::pair<const K *, T> &storage_bucket(int i) const {
        /*
        phm_assert(i >= 0 && i < (int)storage.size())
            << "Out of bounds access: " << i << " " << storage.size() << "\n";
        */
        return storage[i];
    }

    enum {
        Empty = 0,  // No storage allocated
        Small = 1,  // Storage is just an array of key/value pairs
        Large = 2   // Storage is an array with empty slots, indexed by the 'id' field of each key
    } state = Empty;

    void upgrade_from_empty_to_small() {
        storage.resize(max_small_size);
        state = Small;
    }

    void upgrade_from_empty_to_large(int n) {
        storage.resize(n);
        state = Large;
    }

    void upgrade_from_small_to_large(int n) {
        phm_assert(occupied <= max_small_size) << occupied << " " << max_small_size << "\n";
        storage_type tmp(n);
        state = Large;
        tmp.swap(storage);
        int o = occupied;
        for (int i = 0; i < o; i++) {
            emplace_large(tmp[i].first, std::move(tmp[i].second));
        }
        occupied = o;
    }

    // Methods when the map is in the empty state
    T &emplace_empty(const K *n, T &&t) {
        upgrade_from_empty_to_small();
        storage_bucket(0).first = n;
        storage_bucket(0).second = std::move(t);
        occupied = 1;
        return storage_bucket(0).second;
    }

    const T &get_empty(const K *n) const {
        phm_assert(0) << "Calling get on an empty PerfectHashMap";
        return unreachable_value();
    }

    T &get_empty(const K *n) {
        phm_assert(0) << "Calling get on an empty PerfectHashMap";
        return unreachable_value();
    }

    T &get_or_create_empty(const K *n) {
        occupied = 1;
        return emplace_empty(n, T());
    }

    bool contains_empty(const K *n) const {
        return false;
    }

    // Methods when the map is in the small state
    int find_index_small(const K *n) const {
        int i;
        for (i = 0; i < (int)occupied; i++) {
            if (storage_bucket(i).first == n) return i;
        }
        return i;
    }

    T &emplace_small(const K *n, T &&t) {
        int idx = find_index_small(n);
        if (idx >= max_small_size) {
            upgrade_from_small_to_large((int)(n->max_id));
            return emplace_large(n, std::move(t));
        }
        auto &p = storage_bucket(idx);
        if (p.first == nullptr) {
            occupied++;
            p.first = n;
        }
        p.second = std::move(t);
        return p.second;
    }

    const T &get_small(const K *n) const {
        int idx = find_index_small(n);
        return storage_bucket(idx).second;
    }

    T &get_small(const K *n) {
        int idx = find_index_small(n);
        return storage_bucket(idx).second;
    }

    T &get_or_create_small(const K *n) {
        int idx = find_index_small(n);
        if (idx >= max_small_size) {
            upgrade_from_small_to_large((int)(n->max_id));
            return get_or_create_large(n);
        }
        auto &p = storage_bucket(idx);
        if (p.first == nullptr) {
            occupied++;
            p.first = n;
        }
        return p.second;
    }

    bool contains_small(const K *n) const {
        int idx = find_index_small(n);
        return (idx < max_small_size) && (storage_bucket(idx).first == n);
    }

    // Methods when the map is in the large state
    T &emplace_large(const K *n, T &&t) {
        auto &p = storage_bucket(n->id);
        if (!p.first) occupied++;
        p.first = n;
        p.second = std::move(t);
        return p.second;
    }

    const T &get_large(const K *n) const {
        return storage_bucket(n->id).second;
    }

    T &get_large(const K *n) {
        return storage_bucket(n->id).second;
    }

    T &get_or_create_large(const K *n) {
        auto &p = storage_bucket(n->id);
        if (p.first == nullptr) {
            occupied++;
            p.first = n;
        }
        return storage_bucket(n->id).second;
    }

    bool contains_large(const K *n) const {
        return storage_bucket(n->id).first != nullptr;
    }

    void check_key(const K *n) const {
        /*
        phm_assert(n->id >= 0 && n->id < n->max_id)
            << "Invalid hash key: " << n->id << " " << n->max_id << "\n";
        phm_assert(state != Large || (int)storage.size() == n->max_id)
            << "Inconsistent key count: " << n->max_id << " vs " << storage.size() << "\n";
        */
    }

    // Helpers used to pacify compilers
    T &unreachable_value() {
        return storage_bucket(0).second;
    }

    const T &unreachable_value() const {
        return storage_bucket(0).second;
    }

public:
    // Jump straight to the large state
    void make_large(int n) {
        if (state == Empty) {
            upgrade_from_empty_to_large(n);
        } else if (state == Small) {
            upgrade_from_small_to_large(n);
        }
    }

    T &emplace(const K *n, T &&t) {
        check_key(n);
        switch (state) {
        case Empty:
            return emplace_empty(n, std::move(t));
        case Small:
            return emplace_small(n, std::move(t));
        case Large:
            return emplace_large(n, std::move(t));
        }
        return unreachable_value();
    }

    T &insert(const K *n, const T &t) {
        check_key(n);
        T tmp(t);
        switch (state) {
        case Empty:
            return emplace_empty(n, std::move(tmp));
        case Small:
            return emplace_small(n, std::move(tmp));
        case Large:
            return emplace_large(n, std::move(tmp));
        }
        return unreachable_value();
    }

    const T &get(const K *n) const {
        check_key(n);
        switch (state) {
        case Empty:
            return get_empty(n);
        case Small:
            return get_small(n);
        case Large:
            return get_large(n);
        }
        return unreachable_value();
    }

    T &get(const K *n) {
        check_key(n);
        switch (state) {
        case Empty:
            return get_empty(n);
        case Small:
            return get_small(n);
        case Large:
            return get_large(n);
        }
        return unreachable_value();
    }

    T &get_or_create(const K *n) {
        check_key(n);
        switch (state) {
        case Empty:
            return get_or_create_empty(n);
        case Small:
            return get_or_create_small(n);
        case Large:
            return get_or_create_large(n);
        }
        return unreachable_value();
    }

    bool contains(const K *n) const {
        check_key(n);
        switch (state) {
        case Empty:
            return contains_empty(n);
        case Small:
            return contains_small(n);
        case Large:
            return contains_large(n);
        }
        return false;  // Unreachable
    }

    size_t size() const {
        return occupied;
    }

    struct iterator {
        std::pair<const K *, T> *iter, *end;

        void operator++(int) {
            do {
                iter++;
            } while (iter != end && iter->first == nullptr);
        }

        void operator++() {
            (*this)++;
        }

        const K *key() const {
            return iter->first;
        }

        T &value() const {
            return iter->second;
        }

        bool operator!=(const iterator &other) const {
            return iter != other.iter;
        }

        bool operator==(const iterator &other) const {
            return iter == other.iter;
        }

        std::pair<const K *, T> &operator*() {
            return *iter;
        }
    };

    struct const_iterator {
        const std::pair<const K *, T> *iter, *end;

        void operator++(int) {
            do {
                iter++;
            } while (iter != end && iter->first == nullptr);
        }

        void operator++() {
            (*this)++;
        }

        const K *key() const {
            return iter->first;
        }

        const T &value() const {
            return iter->second;
        }

        bool operator!=(const const_iterator &other) const {
            return iter != other.iter;
        }

        bool operator==(const const_iterator &other) const {
            return iter == other.iter;
        }

        const std::pair<const K *, T> &operator*() const {
            return *iter;
        }
    };

    iterator begin() {
        if (state == Empty) return end();
        iterator it;
        it.iter = storage.data();
        it.end = it.iter + storage.size();
        if (it.key() == nullptr) it++;
        phm_assert(it.iter == it.end || it.key());
        return it;
    }

    iterator end() {
        iterator it;
        it.iter = it.end = storage.data() + storage.size();
        return it;
    }

    const_iterator begin() const {
        if (storage.empty()) return end();
        const_iterator it;
        it.iter = storage.data();
        it.end = it.iter + storage.size();
        if (it.key() == nullptr) it++;
        phm_assert(it.iter == it.end || it.key());
        return it;
    }

    const_iterator end() const {
        const_iterator it;
        it.iter = it.end = storage.data() + storage.size();
        return it;
    }
};

#endif
