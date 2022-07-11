#ifndef HALIDE_RUNTIME_STRING_STORAGE_H
#define HALIDE_RUNTIME_STRING_STORAGE_H

#include "block_storage.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Static utility functions for dealing with string data
struct StringUtils {
    static bool is_empty(const char *str) {
        if (str == nullptr) { return true; }
        if (str[0] == '\0') { return true; }
        return false;
    }

    // count the number of delimited string tokens
    static size_t count_tokens(const char *str, const char *delim) {
        if (StringUtils::is_empty(str)) { return 0; }
        if (StringUtils::is_empty(delim)) { return 1; }  // no delim ... string is one token

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

    static size_t count_length(const char *str) {
        const char *ptr = str;
        while (!StringUtils::is_empty(ptr)) {
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
    if (capacity) { contents.reserve(user_context, capacity); }
}

StringStorage::~StringStorage() {
    destroy(nullptr);
}

StringStorage &StringStorage::operator=(const StringStorage &other) {
    if (&other != this) {
        assign(nullptr, other.data(), other.length());
    }
    return *this;
}

bool StringStorage::contains(const char *str) const {
    const char *this_str = static_cast<const char *>(contents.data());
    return strstr(this_str, str) != nullptr;
}

bool StringStorage::contains(const StringStorage &other) const {
    const char *this_str = static_cast<const char *>(contents.data());
    const char *other_str = static_cast<const char *>(other.contents.data());
    return strstr(this_str, other_str) != nullptr;
}

bool StringStorage::operator==(const StringStorage &other) const {
    if (contents.size() != other.contents.size()) { return false; }
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
    contents.resize(user_context, 1);
    char *ptr = static_cast<char *>(contents[0]);
    (*ptr) = ch;
}

void StringStorage::assign(void *user_context, const char *str, size_t length) {
    if (StringUtils::is_empty(str)) { return; }
    if (length == 0) { length = strlen(str); }
    char *this_str = static_cast<char *>(contents.data());
    reserve(user_context, length);
    memcpy(this_str, str, length);
    terminate(user_context, length);
}

void StringStorage::append(void *user_context, const char *str, size_t length) {
    if (StringUtils::is_empty(str)) { return; }
    if (length == 0) { length = strlen(str); }
    const size_t old_size = contents.size();
    size_t new_length = old_size + length;
    char *this_str = static_cast<char *>(contents[old_size]);
    reserve(user_context, length);
    memcpy(this_str, str, length);
    terminate(user_context, new_length);
}

void StringStorage::append(void *user_context, char ch) {
    contents.append(user_context, &ch);
}

void StringStorage::prepend(void *user_context, const char *str, size_t length) {
    if (StringUtils::is_empty(str)) { return; }
    if (length == 0) { length = strlen(str); }
    const size_t old_size = contents.size();
    size_t new_length = old_size + length;
    char *this_str = static_cast<char *>(contents.data());
    reserve(user_context, new_length);
    memcpy(this_str + length, this_str, old_size);
    memcpy(this_str, str, length);
    terminate(user_context, new_length);
}

void StringStorage::prepend(void *user_context, char ch) {
    contents.prepend(user_context, &ch);
}

void StringStorage::terminate(void *user_context, size_t length) {
    char *end_ptr = static_cast<char *>(contents[length]);
    (*end_ptr) = '\0';
}

void StringStorage::clear(void *user_context) {
    contents.clear(user_context);
    if (contents.data()) { terminate(user_context, 0); }
}

void StringStorage::initialize(void *user_context, uint32_t capacity, const SystemMemoryAllocatorFns &sma) {
    contents.initialize(user_context, {sizeof(char), 32, 32}, sma);
    if (capacity) { contents.reserve(user_context, capacity); }
}

void StringStorage::destroy(void *user_context) {
    contents.destroy(user_context);
}

size_t StringStorage::length() const {
    return StringUtils::count_length(data());
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
