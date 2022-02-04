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
};

// --
// Storage class for handling c-string data (based on block storage)
// -- Intended for building and maintaining string data w/8-bit chars
//
class StringStorage {
public:
    typedef BlockStorage<char> CharStorage;

    StringStorage(SystemMemoryAllocator *allocator = default_allocator());
    StringStorage(const StringStorage &other);
    StringStorage(void *user_context, char ch, SystemMemoryAllocator *allocator = default_allocator());
    StringStorage(void *user_context, const char *str, size_t length = 0, SystemMemoryAllocator *allocator = default_allocator());  // if length is zero, strlen is used
    ~StringStorage() = default;

    void initialize(void *user_context, SystemMemoryAllocator *allocator = default_allocator());

    StringStorage &operator=(const StringStorage &other);
    bool operator==(const StringStorage &other) const;
    bool operator!=(const StringStorage &other) const;

    bool contains(const char *str) const;
    bool contains(const StringStorage &other) const;

    void assign(void *user_context, char ch);
    void assign(void *user_context, const char *str, size_t length = 0);  // if length is zero, strlen is used
    void append(void *user_context, char ch);
    void append(void *user_context, const char *str, size_t length = 0);  // if length is zero, strlen is used
    void prepend(void *user_context, char ch);
    void prepend(void *user_context, const char *str, size_t length = 0);  // if length is zero, strlen is used
    void clear(void *user_context);

    size_t length() const;
    const char *data() const;

    SystemMemoryAllocator *current_allocator() const;
    static SystemMemoryAllocator *default_allocator();

private:
    CharStorage contents;
};

StringStorage::StringStorage(SystemMemoryAllocator *sma)
    : contents(sma) {
    // EMPTY
}

StringStorage::StringStorage(const StringStorage &other)
    : contents(other.contents) {
    // EMPTY
}

StringStorage::StringStorage(void *user_context, char ch,
                             SystemMemoryAllocator *sma)
    : contents(sma) {
    assign(user_context, ch);
}

StringStorage::StringStorage(void *user_context, const char *str, size_t length,
                             SystemMemoryAllocator *sma)
    : contents(sma) {
    assign(user_context, str, length);
}

StringStorage &StringStorage::operator=(const StringStorage &other) {
    if (&other != this) {
        assign(nullptr, other.data(), other.length());
    }
    return *this;
}

bool StringStorage::contains(const char *str) const {
    return strstr(contents.data(), str) != nullptr;
}

bool StringStorage::contains(const StringStorage &other) const {
    return strstr(contents.data(), other.contents.data()) != nullptr;
}

bool StringStorage::operator==(const StringStorage &other) const {
    if (contents.size() != other.contents.size()) { return false; }
    return strncmp(contents.data(), other.contents.data(), contents.size()) == 0;
}

bool StringStorage::operator!=(const StringStorage &other) const {
    return !(*this == other);
}

void StringStorage::assign(void *user_context, char ch) {
    contents.resize(user_context, 1);
    contents[0] = ch;
}

void StringStorage::assign(void *user_context, const char *str, size_t length) {
    if (StringUtils::is_empty(str)) { return; }
    if (length == 0) { length = strlen(str); }
    contents.reserve(user_context, length + 1);
    contents.resize(user_context, length, false);
    strncpy(contents.data(), str, length);
    contents[length] = '\0';
}

void StringStorage::append(void *user_context, const char *str, size_t length) {
    if (StringUtils::is_empty(str)) { return; }
    if (length == 0) { length = strlen(str); }
    const size_t old_size = contents.size();
    size_t new_length = old_size + length;
    contents.reserve(user_context, new_length + 1);
    contents.resize(user_context, new_length, false);
    strncpy(contents.data() + old_size, str, length);
    contents[new_length] = '\0';
}

void StringStorage::append(void *user_context, char ch) {
    contents.append(user_context, ch);
}

void StringStorage::prepend(void *user_context, const char *str, size_t length) {
    if (StringUtils::is_empty(str)) { return; }
    if (length == 0) { length = strlen(str); }
    const size_t old_size = contents.size();
    size_t new_length = old_size + length;
    contents.reserve(user_context, new_length + 1);
    contents.resize(user_context, new_length, false);
    strncpy(contents.data() + length, contents.data(), old_size);
    strncpy(contents.data(), str, length);
    contents[new_length] = '\0';
}

void StringStorage::prepend(void *user_context, char ch) {
    contents.prepend(user_context, ch);
}

void StringStorage::clear(void *user_context) {
    contents.clear(user_context);
}

void StringStorage::initialize(void *user_context, SystemMemoryAllocator *sma) {
    contents.initialize(user_context, sma);
}

size_t StringStorage::length() const {
    return contents.size();
}

const char *StringStorage::data() const {
    return contents.data();
}

SystemMemoryAllocator *
StringStorage::current_allocator() const {
    return contents.current_allocator();
}

SystemMemoryAllocator *
StringStorage::default_allocator() {
    return CharStorage::default_allocator();
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_STRING_STORAGE_H