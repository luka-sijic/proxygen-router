#pragma once
#include <memory>
#include <mutex>
#include <pqxx/pqxx>
#include <queue>

class DBPool {
public:
  DBPool(const std::string &conninfo, size_t size = 4) {
    for (size_t i = 0; i < size; i++) {
      pool_.push(std::make_shared<pqxx::connection>(conninfo));
    }
  }

  std::shared_ptr<pqxx::connection> acquire() {
    std::lock_guard<std::mutex> lk(mu_);
    if (pool_.empty())
      throw std::runtime_error("No DB connections available");
    auto c = pool_.front();
    pool_.pop();
    return c;
  }

  void release(std::shared_ptr<pqxx::connection> c) {
    std::lock_guard<std::mutex> lk(mu_);
    pool_.push(std::move(c));
  }

private:
  std::queue<std::shared_ptr<pqxx::connection>> pool_;
  std::mutex mu_;
};
