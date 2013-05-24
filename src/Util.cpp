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

string unique_name(const string &name) {
    static map<string, int> known_names;

    // If the programmer specified a single character name then use the
    // pre-existing Halide unique name generator.
    if (name.length() == 1) {
        return unique_name(name[0]);
    }    

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
