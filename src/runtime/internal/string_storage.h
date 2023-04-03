#ifndef HALIDE_RUNTIME_STRING_STORAGE_H
#define HALIDE_RUNTIME_STRING_STORAGE_H

#include "HalideRuntime.h"
#include "block_storage.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Static utility functions for dealing with string data
struct StringUtils {
    static bool is_empty(const char *str) {
        if (str == nullptr) {
            return true;
        }
        if (str[0] == '\0') {
            return true;
        }
        return false;
    }

    // count the number of delimited string tokens
    static size_t count_tokens(const char *str, const char *delim) {
        if (StringUtils::is_empty(str)) {
            return 0;
        }
        if (StringUtils::is_empty(delim)) {
            return 1;
        }  // no delim ... string is one token

        size_t count = 0;
        const char *ptr = str;
        size_t delim_length = strlen(delim);
        while (!StringUtils::is_empty(ptr)) {
            const char *next_delim = strstr(ptr, delim);
            ptr = (next_delim != nullptr) ? (next_delim + delim_length) : nullptr;
            ++count;
        }
        return count;
    }

    // retuns true if s1 contains s2 (within n characters)
    static bool contains(const char *s1, const char *s2, size_t n) {
        if (is_empty(s2)) {
            return true;
        }  // s2 is empty ... return true to match strstr
        char starts_with = *s2;
        for (size_t length = strlen(s2); length <= n; n--, s1++) {
            if (*s1 == starts_with) {
                for (size_t i = 1; i <= length; i++) {
                    if (i == length) {
                        return true;
                    }
                    if (s1[i] != s2[i]) {
                        break;
                    }
                }
            }
        }
        return false;
    }

    static size_t count_length(const char *str, size_t max_chars) {
        const char *ptr = str;
        while (!StringUtils::is_empty(ptr) && ((size_t(ptr - str)) < max_chars)) {
            ++ptr;
        }
        return size_t(ptr - str);
    }
};

// --
// Storage class for handling c-string data (based on block storage)
// -- Intended for building and maintaining string data w/8-bit chars
//
class StringStorage {
public:
    StringStorage(void *user_context = nullptr, uint32_t capacity = 0, const SystemMemoryAllocatorFns &sma = default_allocator());
    StringStorage(const StringStorage &other) = default;
    ~StringStorage();

    // Factory methods for creation / destruction
    static StringStorage *create(void *user_context, const SystemMemoryAllocatorFns &ma);
    static void destroy(void *user_context, StringStorage *string_storage);

    void initialize(void *user_context, uint32_t capacity = 0, const SystemMemoryAllocatorFns &sma = default_allocator());
    void destroy(void *user_context);

    StringStorage &operator=(const StringStorage &other);
    bool operator==(const StringStorage &other) const;
    bool operator!=(const StringStorage &other) const;

    bool contains(const char *str) const;
    bool contains(const StringStorage &other) const;

    void reserve(void *user_context, size_t length);
    void assign(void *user_context, char ch);
    void assign(void *user_context, const char *str, size_t length = 0);  // if length is zero, strlen is used
    void append(void *user_context, char ch);
    void append(void *user_context, const char *str, size_t length = 0);  // if length is zero, strlen is used
    void prepend(void *user_context, char ch);
    void prepend(void *user_context, const char *str, size_t length = 0);  // if length is zero, strlen is used
    void clear(void *user_context);
    void terminate(void *user_context, size_t length);

    size_t length() const;
    const char *data() const;

    const SystemMemoryAllocatorFns &current_allocator() const;
    static const SystemMemoryAllocatorFns &default_allocator();

private:
    BlockStorage contents;
};

StringStorage::StringStorage(void *user_context, uint32_t capacity, const SystemMemoryAllocatorFns &sma)
    : contents(user_context, {sizeof(char), 32, 32}, sma) {
    if (capacity) {
        contents.reserve(user_context, capacity);
    }
}

StringStorage::~StringStorage() {
    destroy(nullptr);
}

StringStorage *StringStorage::create(void *user_context, const SystemMemoryAllocatorFns &system_allocator) {
    halide_debug_assert(user_context, system_allocator.allocate != nullptr);
    StringStorage *result = reinterpret_cast<StringStorage *>(
        system_allocator.allocate(user_context, sizeof(StringStorage)));

    if (result == nullptr) {
        halide_error(user_context, "StringStorage: Failed to create instance! Out of memory!\n");
        return nullptr;
    }

    result->initialize(user_context, 32, system_allocator);
    return result;
}

void StringStorage::destroy(void *user_context, StringStorage *instance) {
    halide_debug_assert(user_context, instance != nullptr);
    const SystemMemoryAllocatorFns &system_allocator = instance->current_allocator();
    instance->destroy(user_context);
    halide_debug_assert(user_context, system_allocator.deallocate != nullptr);
    system_allocator.deallocate(user_context, instance);
}

StringStorage &StringStorage::operator=(const StringStorage &other) {
    if (&other != this) {
        assign(nullptr, other.data(), other.length());
    }
    return *this;
}

bool StringStorage::contains(const char *str) const {
    if (contents.empty()) {
        return false;
    }
    const char *this_str = static_cast<const char *>(contents.data());
    return StringUtils::contains(this_str, str, contents.size());
}

bool StringStorage::contains(const StringStorage &other) const {
    if (contents.empty()) {
        return false;
    }
    if (other.contents.empty()) {
        return false;
    }
    const char *this_str = static_cast<const char *>(contents.data());
    const char *other_str = static_cast<const char *>(other.contents.data());
    return StringUtils::contains(this_str, other_str, contents.size());
}

bool StringStorage::operator==(const StringStorage &other) const {
    if (contents.size() != other.contents.size()) {
        return false;
    }
    const char *this_str = static_cast<const char *>(contents.data());
    const char *other_str = static_cast<const char *>(other.contents.data());
    return strncmp(this_str, other_str, contents.size()) == 0;
}

bool StringStorage::operator!=(const StringStorage &other) const {
    return !(*this == other);
}

void StringStorage::reserve(void *user_context, size_t length) {
    contents.reserve(user_context, length + 1);  // leave room for termination
    contents.resize(user_context, length, false);
    terminate(user_context, length);
}

void StringStorage::assign(void *user_context, char ch) {
    reserve(user_context, 1);
    char *ptr = static_cast<char *>(contents[0]);
    (*ptr) = ch;
    terminate(user_context, 1);
}

void StringStorage::assign(void *user_context, const char *str, size_t length) {
    if (StringUtils::is_empty(str)) {
        return;
    }
    if (length == 0) {
        length = strlen(str);
    }
    reserve(user_context, length);
    contents.replace(user_context, 0, str, length);
    terminate(user_context, length);
}

void StringStorage::append(void *user_context, const char *str, size_t length) {
    if (StringUtils::is_empty(str)) {
        return;
    }
    if (length == 0) {
        length = strlen(str);
    }
    const size_t old_length = StringUtils::count_length(data(), contents.size());
    size_t new_length = old_length + length;
    reserve(user_context, new_length);
    contents.insert(user_context, old_length, str, length);
    terminate(user_context, new_length);
}

void StringStorage::append(void *user_context, char ch) {
    const size_t old_length = StringUtils::count_length(data(), contents.size());
    size_t new_length = old_length + 1;
    reserve(user_context, new_length);
    contents.insert(user_context, old_length, &ch, 1);
    terminate(user_context, new_length);
}

void StringStorage::prepend(void *user_context, const char *str, size_t length) {
    if (StringUtils::is_empty(str)) {
        return;
    }
    if (length == 0) {
        length = strlen(str);
    }
    const size_t old_length = StringUtils::count_length(data(), contents.size());
    size_t new_length = old_length + length;
    reserve(user_context, new_length);
    contents.insert(user_context, 0, str, length);
    terminate(user_context, new_length);
}

void StringStorage::prepend(void *user_context, char ch) {
    const size_t old_length = StringUtils::count_length(data(), contents.size());
    size_t new_length = old_length + 1;
    reserve(user_context, new_length);
    contents.prepend(user_context, &ch);
    terminate(user_context, new_length);
}

void StringStorage::terminate(void *user_context, size_t length) {
    if (contents.data() && (length < contents.size())) {
        char *end_ptr = static_cast<char *>(contents[length]);
        (*end_ptr) = '\0';
    }
}

void StringStorage::clear(void *user_context) {
    contents.clear(user_context);
    terminate(user_context, 0);
}

void StringStorage::initialize(void *user_context, uint32_t capacity, const SystemMemoryAllocatorFns &sma) {
    contents.initialize(user_context, {sizeof(char), 32, 32}, sma);
    reserve(user_context, capacity);
    terminate(user_context, 0);
}

void StringStorage::destroy(void *user_context) {
    contents.destroy(user_context);
}

size_t StringStorage::length() const {
    return StringUtils::count_length(data(), contents.size());
}

const char *StringStorage::data() const {
    return static_cast<const char *>(contents.data());
}

const SystemMemoryAllocatorFns &
StringStorage::current_allocator() const {
    return contents.current_allocator();
}

const SystemMemoryAllocatorFns &
StringStorage::default_allocator() {
    return BlockStorage::default_allocator();
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_STRING_STORAGE_H
