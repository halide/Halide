#ifndef HALIDE_RUNTIME_STRING_TABLE_H
#define HALIDE_RUNTIME_STRING_TABLE_H

#include "../HalideRuntime.h"
#include "block_storage.h"
#include "pointer_table.h"
#include "string_storage.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Storage class for an array of strings (based on block storage)
// -- Intended for building and maintaining tables of strings
class StringTable {
public:
    // Disable copy constructors
    StringTable(const StringTable &) = delete;
    StringTable &operator=(const StringTable &) = delete;

    explicit StringTable(const SystemMemoryAllocatorFns &allocator = StringStorage::default_allocator());
    StringTable(void *user_context, size_t capacity, const SystemMemoryAllocatorFns &allocator = StringStorage::default_allocator());
    StringTable(void *user_context, const char **array, size_t count, const SystemMemoryAllocatorFns &allocator = StringStorage::default_allocator());
    ~StringTable();

    void resize(void *user_context, size_t capacity);
    void destroy(void *user_context);
    void clear(void *user_context);

    // fills the contents of the table (copies strings from given array)
    void fill(void *user_context, const char **array, size_t coun);

    // assign the entry at given index the given string
    void assign(void *user_context, size_t index, const char *str, size_t length = 0);  // if length is zero, strlen is used

    // appends the given string to the end of the table
    void append(void *user_context, const char *str, size_t length = 0);  // if length is zero, strlen is used

    // prepend the given string to the end of the table
    void prepend(void *user_context, const char *str, size_t length = 0);  // if length is zero, strlen is used

    // parses the given c-string based on given delimiter, stores each substring in the resulting table
    size_t parse(void *user_context, const char *str, const char *delim);

    // index-based access operator
    const char *operator[](size_t index) const;

    // returns the raw string table pointer
    const char **data() const;

    // scans the table for existance of the given string within any entry (linear scan w/string compare!)
    bool contains(const char *str) const;

    size_t size() const {
        return contents.size();
    }

private:
    PointerTable contents;  //< owns string data
    PointerTable pointers;  //< pointers to raw string data
};

// --

StringTable::StringTable(const SystemMemoryAllocatorFns &sma)
    : contents(nullptr, 0, sma),
      pointers(nullptr, 0, sma) {
    // EMPTY!
}

StringTable::StringTable(void *user_context, size_t capacity, const SystemMemoryAllocatorFns &sma)
    : contents(user_context, capacity, sma),
      pointers(user_context, capacity, sma) {
    if (capacity) {
        resize(user_context, capacity);
    }
}

StringTable::StringTable(void *user_context, const char **array, size_t count, const SystemMemoryAllocatorFns &sma)
    : contents(user_context, count, sma),
      pointers(user_context, count, sma) {
    fill(user_context, array, count);
}

StringTable::~StringTable() {
    destroy(nullptr);
}

void StringTable::resize(void *user_context, size_t capacity) {
    pointers.resize(user_context, capacity);
    while (contents.size() < capacity) {
        StringStorage *storage_ptr = StringStorage::create(user_context, contents.current_allocator());
        contents.append(user_context, storage_ptr);
    }
}

void StringTable::clear(void *user_context) {
    for (size_t n = 0; n < contents.size(); ++n) {
        StringStorage *storage_ptr = static_cast<StringStorage *>(contents[n]);
        StringStorage::destroy(user_context, storage_ptr);
        contents.assign(user_context, n, nullptr);
    }
    contents.clear(user_context);
    pointers.clear(user_context);
}

void StringTable::destroy(void *user_context) {
    for (size_t n = 0; n < contents.size(); ++n) {
        StringStorage *storage_ptr = static_cast<StringStorage *>(contents[n]);
        StringStorage::destroy(user_context, storage_ptr);
        contents.assign(user_context, n, nullptr);
    }
    contents.destroy(user_context);
    pointers.destroy(user_context);
}

const char *StringTable::operator[](size_t index) const {
    if (index < pointers.size()) {
        return static_cast<const char *>(pointers[index]);
    }
    return nullptr;
}

void StringTable::fill(void *user_context, const char **array, size_t count) {
    resize(user_context, count);
    for (size_t n = 0; n < count && n < contents.size(); ++n) {
        StringStorage *storage_ptr = static_cast<StringStorage *>(contents[n]);
        storage_ptr->assign(user_context, array[n]);
        pointers.assign(user_context, n, storage_ptr->data());
    }
}

void StringTable::assign(void *user_context, size_t index, const char *str, size_t length) {
    if (length == 0) {
        length = strlen(str);
    }
    if (index < contents.size()) {
        StringStorage *storage_ptr = static_cast<StringStorage *>(contents[index]);
        storage_ptr->assign(user_context, str, length);
        pointers.assign(user_context, index, storage_ptr->data());
    }
}

void StringTable::append(void *user_context, const char *str, size_t length) {
    StringStorage *storage_ptr = StringStorage::create(user_context, contents.current_allocator());
    storage_ptr->assign(user_context, str, length);
    contents.append(user_context, storage_ptr);
    pointers.append(user_context, storage_ptr->data());
}

void StringTable::prepend(void *user_context, const char *str, size_t length) {
    StringStorage *storage_ptr = StringStorage::create(user_context, contents.current_allocator());
    storage_ptr->assign(user_context, str, length);
    contents.prepend(user_context, storage_ptr);
    pointers.prepend(user_context, storage_ptr->data());
}

size_t StringTable::parse(void *user_context, const char *str, const char *delim) {
    if (StringUtils::is_empty(str)) {
        return 0;
    }

    size_t delim_length = strlen(delim);
    size_t total_length = strlen(str);
    size_t entry_count = StringUtils::count_tokens(str, delim);
    if (entry_count < 1) {
        return 0;
    }

    resize(user_context, entry_count);

    // save each entry into the table
    size_t index = 0;
    const char *ptr = str;
    while (!StringUtils::is_empty(ptr) && (index < entry_count)) {
        size_t ptr_offset = ptr - str;
        const char *next_delim = strstr(ptr, delim);
        size_t token_length = (next_delim == nullptr) ? (total_length - ptr_offset) : (next_delim - ptr);
        if (token_length > 0 && index < contents.size()) {
            StringStorage *storage_ptr = static_cast<StringStorage *>(contents[index]);
            storage_ptr->assign(user_context, ptr, token_length);
            pointers.assign(user_context, index, storage_ptr->data());
            ++index;
        }
        ptr = (next_delim != nullptr) ? (next_delim + delim_length) : nullptr;
    }
    return entry_count;
}

bool StringTable::contains(const char *str) const {
    if (StringUtils::is_empty(str)) {
        return false;
    }
    for (size_t n = 0; n < contents.size(); ++n) {
        StringStorage *storage_ptr = static_cast<StringStorage *>(contents[n]);
        if (storage_ptr->contains(str)) {
            return true;
        }
    }

    return false;
}

const char **StringTable::data() const {
    return reinterpret_cast<const char **>(pointers.data());
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_STRING_STORAGE_H
