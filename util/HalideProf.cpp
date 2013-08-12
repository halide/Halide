#include <algorithm>
#include <iomanip>
#include <ios>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <stdint.h>
#include <stdio.h>

namespace {

  const char kToplevel[] = "$total$";

  struct OpInfo {
    std::string op_type;
    std::string op_name;
    std::string parent_type;
    std::string parent_name;
    // number of times called
    uint64_t count;
    // ticks used (processor specific, no fixed time intercal)
    uint64_t ticks;
    // usec is actually only measured for $total$;
    // it's (approximated) calculated for all others
    double usec;
    // percentage of total ticks, [0.0..1.0]
    double percent;
    // ticks/usec/percent used by this op alone (not including callees)
    uint64_t ticks_only;
    double usec_only;
    double percent_only;

    OpInfo()
      : count(0),
        ticks(0),
        usec(0.0),
        percent(0.0) {}
  };

  // Outer map is keyed by function name,
  // inner map is keyed by op_type + op_name
  typedef std::map<std::string, OpInfo> OpInfoMap;
  typedef std::map<std::string, OpInfoMap> FuncInfoMap;

  std::string qualified_name(const std::string& op_type, const std::string& op_name) {
    // Aribtrary, just join type + name
    return op_type + ":" + op_name;
  }

  // How is it posible that there is no string-split function in the C++
  // standard library?
  std::vector<std::string> Split(const std::string& s, char delim) {
    std::vector<std::string> v;
    std::istringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        v.push_back(item);
    }
    return v;
  }

  bool HasOpt(char** begin, char** end, const std::string& opt) {
    return std::find(begin, end, opt) != end;
  }

  // look for string "opt"; if found, return subsequent string;
  // if not found, return empty string.
  std::string GetOpt(char** begin, char** end, const std::string& opt) {
    char** it = std::find(begin, end, opt);
    if (it != end && ++it != end) {
        return std::string(*it);
    }
    return std::string();
  }

  void ProcessLine(const std::string& s, FuncInfoMap& info) {
    std::vector<std::string> v = Split(s, ' ');
    if (v.size() < 8 || v[0] != "halide_profiler") {
      return;
    }
    const std::string& metric = v[1];
    const std::string& func_name = v[2];
    const std::string& op_type = v[3];
    const std::string& op_name = v[4];
    const std::string& parent_type = v[5];
    const std::string& parent_name = v[6];
    std::istringstream value_stream(v[7]);
    uint64_t value;
    value_stream >> value;
    OpInfoMap& op_info_map = info[func_name];
    OpInfo& op_info = op_info_map[qualified_name(op_type, op_name)];
    op_info.op_type = op_type;
    op_info.op_name = op_name;
    op_info.parent_type = parent_type;
    op_info.parent_name = parent_name;
    if (metric == "count") {
      op_info.count = value;
    } else if (metric == "ticks") {
      op_info.ticks = value;
    } else if (metric == "usec") {
      op_info.usec = value;
    }
  }

  void FinishOpInfo(OpInfoMap& op_info_map) {
    // First, fill in usec and percent.
    std::string toplevel_qual_name = qualified_name(kToplevel, kToplevel);

    OpInfo& total = op_info_map[toplevel_qual_name];
    total.count = 1;
    total.percent = 1.0;

    double ticks_per_usec = (double)total.ticks / (double)total.usec;
    for (OpInfoMap::iterator o = op_info_map.begin(); o != op_info_map.end(); ++o) {
      const std::string& qual_name = o->first;
      if (qual_name == toplevel_qual_name) {
        continue;
      }
      OpInfo& op_info = o->second;
      // usec isn't guaranteed to be super-precise, so this
      // isn't perfect; still useful for estimation though.
      op_info.usec = op_info.ticks / ticks_per_usec;
      op_info.percent = (double)op_info.ticks / (double)total.ticks;
    }

    // Now, fill in the "only" fields. First a map from parent -> children...
    std::map<std::string, std::vector<OpInfo> > children_map;
    for (OpInfoMap::iterator o = op_info_map.begin(); o != op_info_map.end(); ++o) {
      OpInfo& op_info = o->second;
      std::string parent_qual_name = qualified_name(op_info.parent_type, op_info.parent_name);
      children_map[parent_qual_name].push_back(op_info);
    }

    // ... then adjust the "only" fields.
    for (OpInfoMap::iterator o = op_info_map.begin(); o != op_info_map.end(); ++o) {
      OpInfo& op_info = o->second;
      op_info.ticks_only = op_info.ticks;
      op_info.usec_only = op_info.usec;
      std::string qual_name = qualified_name(op_info.op_type, op_info.op_name);
      const std::vector<OpInfo>& children = children_map[qual_name];
      for (std::vector<OpInfo>::const_iterator c = children.begin(); c != children.end(); ++c) {
        op_info.ticks_only -= c->ticks;
        op_info.usec_only -= c->usec;
      }
      op_info.percent_only = (double)op_info.ticks_only / (double)total.ticks;
    }
  }

  template <typename CmpFunc>
  std::vector<OpInfo> SortOpInfo(const OpInfoMap& op_info_map, CmpFunc cmp) {
    std::vector<OpInfo> v;
    for (OpInfoMap::const_iterator o = op_info_map.begin(); o != op_info_map.end(); ++o) {
      v.push_back(o->second);
    }
    // Note: using reverse iterators to get a descending sort
    std::sort(v.rbegin(), v.rend(), cmp);
    return v;
  }

  bool by_count(const OpInfo& a, const OpInfo& b) { return a.count < b.count; }
  bool by_ticks(const OpInfo& a, const OpInfo& b) { return a.ticks < b.ticks; }
  bool by_ticks_only(const OpInfo& a, const OpInfo& b) { return a.ticks_only < b.ticks_only; }

}  // namespace

int main(int argc, char** argv) {

  if (HasOpt(argv, argv + argc, "-h")) {
    printf("HalideProf [-f funcname] [-sort c|t|to] [-top N] < profiledata\n");
    return 0;
  }

  std::string func_name_filter = GetOpt(argv, argv + argc, "-f");

  bool (*sort_by_func)(const OpInfo& a, const OpInfo& b);
  std::string sort_by = GetOpt(argv, argv + argc, "-sort");
  if (sort_by.empty() || sort_by == "to") {
    sort_by_func = by_ticks_only;
  } else if (sort_by == "t") {
    sort_by_func = by_ticks;
  } else if (sort_by == "c") {
    sort_by_func = by_count;
  } else {
    std::cerr << "Unknown value for -sort: " << sort_by << "\n";
    exit(-1);
  }

  int32_t top_n = 10;
  std::string top_n_str = GetOpt(argv, argv + argc, "-top");
  if (!top_n_str.empty()) {
    std::istringstream(top_n_str) >> top_n;
  }

  FuncInfoMap func_info_map;
  std::string line;
  while (std::getline(std::cin, line)) {
    ProcessLine(line, func_info_map);
  }

  for (FuncInfoMap::iterator f = func_info_map.begin(); f != func_info_map.end(); ++f) {
    FinishOpInfo(f->second);
  }

  for (FuncInfoMap::iterator f = func_info_map.begin(); f != func_info_map.end(); ++f) {
    const std::string& func_name = f->first;
    if (!func_name_filter.empty() && func_name_filter != func_name) {
      continue;
    }
    std::cout << "Func: " << func_name << "\n";
    std::cout << "--------------------------\n";
    std::cout
      << std::setw(10) << std::left << "op_type"
      << std::setw(40) << std::left << "op_name"
      << std::setw(16) << std::right << "count"
      << std::setw(16) << "ticks-cum"
      << std::setw(12) << "msec-cum"
      << std::setw(8) << std::fixed << "%-cum"
      << std::setw(16) << "ticks-only"
      << std::setw(12) << "msec-only"
      << std::setw(8) << std::fixed << "%-only"
      << "\n";
    std::vector<OpInfo> op_info = SortOpInfo(f->second, sort_by_func);
    for (std::vector<OpInfo>::const_iterator o = op_info.begin(); o != op_info.end(); ++o) {
      const OpInfo& op_info = *o;
      std::cout
        << std::setw(10) << std::left << op_info.op_type
        << std::setw(40) << std::left << op_info.op_name
        << std::setw(16) << std::right << op_info.count
        << std::setw(16) << op_info.ticks
        << std::setw(12) << std::setprecision(2) << std::fixed << (op_info.usec / 1000.0)
        << std::setw(8) << std::setprecision(2) << std::fixed << (op_info.percent * 100.0)
        << std::setw(16) << op_info.ticks_only
        << std::setw(12) << std::setprecision(2) << std::fixed << (op_info.usec_only / 1000.0)
        << std::setw(8) << std::setprecision(2) << std::fixed << (op_info.percent_only * 100.0)
        << "\n";
      if (--top_n <= 0) {
        break;
      }
    }
  }
}


