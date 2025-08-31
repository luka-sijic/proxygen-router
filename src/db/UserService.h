#pragma once
#include <iostream>
#include <nlohmann/json.hpp>

#include "DB.h"

class UserService {
 public:
  explicit UserService(DBPool& pool) : pool_(pool) {}

  std::string registerUser(const std::string& username, const std::string& password, const std::string& email) {
    try {
      auto conn = pool_.acquire();
      pqxx::work txn(*conn);
      auto r = txn.exec("INSERT INTO users (username, password, email) VALUES ($1,$2,$3)", pqxx::params(username, password, email));
      txn.commit();
      pool_.release(conn);
      return "success";
    } catch (...) {
      std::cout << "Error" << std::endl;
      return "false";
    }
  }

  nlohmann::json getUserById(int id) {
    auto conn = pool_.acquire();
    pqxx::work txn(*conn);
    auto r = txn.exec("SELECT id, username, email FROM users WHERE id=$1",
                        pqxx::params(id));
    txn.commit();
    pool_.release(conn);

    if (r.empty()) {
      return nlohmann::json::object({{"error", "not_found"}});
    }

    const pqxx::row row = r[0];
    return nlohmann::json{{"id", row["id"].as<int>()},
                          {"username", row["username"].as<std::string>()},
                          {"email", row["email"].as<std::string>()}};
  }

 private:
  DBPool& pool_;
};
