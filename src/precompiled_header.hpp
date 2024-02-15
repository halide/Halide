#pragma once

// Below is a list of common STL includes in the Halide codebase.
// To find them, you can use:
//
// ag --nofilename --nonumbers --nobreak "#include <" src include | sort | uniq -c | sort -h
// I'm explicitly exluding the test folder, because somebody already set up
// a precompiled header for the test directory.

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <cstring>
#include <limits>
#include <set>
#include <stdint.h>
#include <iostream>
#include <cstdint>
#include <utility>
#include <memory>
#include <map>
#include <vector>
#include <string>
