#ifndef PARSE_H
#define PARSE_H

#include "Errors.h"
#include <sstream>
#include <type_traits>

namespace Halide {
namespace Internal {
namespace Autoscheduler {

class ParamParser {
    std::map<std::string, std::string> extra;

    // If the string can be parsed as a valid "T", set *value to it.
    // If not, assert-fail.
    template<typename T>
    static void parse_or_die(const std::string &str, T *value) {
        std::istringstream iss(str);
        T t;
        // All one-byte ints int8 and uint8 should be parsed as integers, not chars --
        // including 'char' itself. (Note that sizeof(bool) is often-but-not-always-1,
        // so be sure to exclude that case.)
        if constexpr (sizeof(T) == sizeof(char) && !std::is_same<T, bool>::value) {
            int i;
            iss >> i;
            t = (T)i;
        } else {
            iss >> t;
        }
        user_assert(!iss.fail() && iss.get() == EOF) << "Unable to parse: " << str;
        *value = t;
    }

public:
    explicit ParamParser(const std::map<std::string, std::string> &m)
        : extra(m) {
    }

    // If the given key is present in m, parse the result into *value and return true.
    // (If the string cannot be parsed as a valid "T", assert-fail.)
    // If the given key is not present, leave *value untouched and return false.
    template<typename T>
    bool parse(const std::string &key, T *value) {
        auto it = extra.find(key);
        if (it == extra.end()) {
            return false;
        }
        parse_or_die(it->second, value);
        extra.erase(it);
        return true;
    }

    void finish() {
        if (!extra.empty()) {
            std::ostringstream oss;
            oss << "Autoscheduler Params contain unknown keys:\n";
            for (const auto &it : extra) {
                oss << "  " << it.first << "\n";
            }
            user_error << oss.str();
        }
    }

    ~ParamParser() {
        finish();
    }
};

}  // namespace Autoscheduler
}  // namespace Internal
}  // namespace Halide

#endif
