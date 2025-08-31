#pragma once
#include <string>
#include <vector>
#include <unordered_map>

enum class SegType { Literal, Param, Wildcard };

struct Seg { SegType type; std::string name; };

inline std::string stripQuery(std::string s) {
  auto q = s.find('?');
  if (q != std::string::npos) s.resize(q);
  if (s.size() > 1 && s.back() == '/') s.pop_back();
  return s;
}

inline std::vector<std::string> splitPath(const std::string& p) {
  std::vector<std::string> out; size_t i=0,n=p.size();
  while (i<n) {
    while (i<n && p[i]=='/') ++i;
    if (i>=n) break;
    size_t j=i; while (j<n && p[j]!='/') ++j;
    out.emplace_back(p.substr(i,j-i));
    i=j;
  }
  return out;
}

inline std::vector<Seg> compilePattern(const std::string& pattern) {
  std::vector<Seg> segs;
  for (auto& s : splitPath(stripQuery(pattern))) {
    if (!s.empty() && s[0]==':') segs.push_back({SegType::Param, s.substr(1)});
    else if (!s.empty() && s[0]=='*') { segs.push_back({SegType::Wildcard, s.substr(1)}); break; }
    else segs.push_back({SegType::Literal, s});
  }
  return segs;
}

inline bool matchPattern(const std::vector<Seg>& segs,
                         const std::vector<std::string>& parts,
                         std::unordered_map<std::string,std::string>& out) {
  out.clear(); size_t i=0,j=0;
  while (i<segs.size() && j<parts.size()) {
    const auto& seg = segs[i]; const auto& part = parts[j];
    if (seg.type==SegType::Literal) { if (seg.name!=part) return false; ++i;++j; }
    else if (seg.type==SegType::Param) { out[seg.name]=part; ++i;++j; }
    else { // Wildcard
      std::string rest;
      for (size_t k=j;k<parts.size();++k) { if(k>j) rest.push_back('/'); rest+=parts[k]; }
      out[seg.name]=rest; return true;
    }
  }
  return (i==segs.size() && j==parts.size());
}
