#pragma once
#include <string>
#include <unordered_map>
#include <chrono>

struct RouteContext {
  std::string method;
  std::string path;
  std::unordered_map<std::string,std::string> params;
  std::unordered_map<std::string,std::string> reqHeaders;
  std::string requestId;
  std::chrono::steady_clock::time_point start;

  const std::string& param(const std::string& k, const std::string& d="") const {
    auto it = params.find(k); return it==params.end()? d : it->second;
  }
  std::string header(std::string k, const std::string& d="") const {
    for (auto& c: k) c = char(::tolower(c));
    auto it = reqHeaders.find(k); return it==reqHeaders.end()? d : it->second;
  }
};
