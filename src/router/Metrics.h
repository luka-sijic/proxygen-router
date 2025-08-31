#pragma once
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <string>

struct Metrics {
  std::atomic<uint64_t> total{0}, in_flight{0}, errors{0};
  std::mutex mu;
  std::unordered_map<std::string,std::pair<uint64_t,double>> by_route;

  void record(const std::string& key,double ms,bool error){
    total++; if(error) errors++;
    std::lock_guard<std::mutex> lk(mu);
    auto& p=by_route[key]; p.first++; p.second+=ms;
  }
  std::string render(){
    std::string s;
    s += "http_requests_total " + std::to_string(total.load()) + "\n";
    s += "http_in_flight " + std::to_string(in_flight.load()) + "\n";
    s += "http_request_errors_total " + std::to_string(errors.load()) + "\n";
    for(auto& kv: by_route){
      s += "http_request_duration_ms{route=\"" + kv.first + "\"} " + std::to_string(kv.second.second) + "\n";
      s += "http_requests_by_route_total{route=\"" + kv.first + "\"} " + std::to_string(kv.second.first) + "\n";
    }
    return s;
  }
};
