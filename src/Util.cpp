#include "Util.h"
#include <sstream>
#include <map>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;
using std::ostringstream;
using std::map;

string unique_name(char prefix) {
    // arrays with static storage duration should be initialized to zero automatically
    static int instances[256];
    ostringstream str;
    str << prefix << instances[(unsigned char)prefix]++;
    return str.str();
}

bool starts_with(const string &str, const string &prefix) {
    if (str.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); i++) {
        if (str[i] != prefix[i]) return false;
    }
    return true;
}

bool ends_with(const string &str, const string &suffix) {
    if (str.size() < suffix.size()) return false;
    size_t off = str.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); i++) {
        if (str[off+i] != suffix[i]) return false;
    }
    return true;
}

/** Convert an integer to a string. */
string int_to_string(int x) {
    // Most calls to this function are during lowering, and correspond
    // to the dimensions of some buffer. So this gets called with 0,
    // 1, 2, and 3 a lot, and it's worth optimizing those cases.
    static const string small_ints[] = {"0", "1", "2", "3", "4", "5", "6", "7"};
    if (x < 8) return small_ints[x];
    ostringstream ss;
    ss << x;
    return ss.str();
}

string unique_name(const string &name) {
    static map<string, int> known_names;

    // An empty string really does not make sense, but use 'z' as prefix.
    if (name.length() == 0) {
        return unique_name('z');
    }

    // Check the '$' character doesn't appear in the prefix. This lets
    // us separate the name from the number using '$' as a delimiter,
    // which guarantees uniqueness of the generated name, without
    // having to track all name generated so far.
    for (size_t i = 0; i < name.length(); i++) {
        assert(name[i] != '$' && "names passed to unique_name may not contain the character '$'");
    }

    int &count = known_names[name];
    count++;
    if (count == 1) {
        // The very first unique name is the original function name itself.
        return name;
    } else {
        // Use the programmer-specified name but append a number to make it unique.
        ostringstream oss;
        oss << name << '$' << count;
        return oss.str();
    }
}

string base_name(const string &name) {
    size_t off = name.rfind('.');
    if (off == string::npos) {
        return "";
    }
    return name.substr(off+1);
}

}
}
