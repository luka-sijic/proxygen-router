#pragma once

#include "RouteContext.h"
#include "Response.h"
#include "Middleware.h"
#include "Metrics.h"

#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class RouterFactory : public proxygen::RequestHandlerFactory {
 public:
  using HandlerFnWithBody = std::function<void(const std::string&, Res&)>;
  using HandlerFnNoBody   = std::function<void(Res&)>;

  RouterFactory();

  // Proxygen interface
  void onServerStart(folly::EventBase*) noexcept override;
  void onServerStop() noexcept override;
  proxygen::RequestHandler* onRequest(
      proxygen::RequestHandler*,
      proxygen::HTTPMessage* msg) noexcept override;

  // ----------------------------
  // Route registration (verbs)
  // ----------------------------
  void get   (const std::string& path, HandlerFnNoBody fn);
  void head  (const std::string& path, HandlerFnNoBody fn);
  void post  (const std::string& path, HandlerFnNoBody fn);
  void post  (const std::string& path, HandlerFnWithBody fn);
  void put   (const std::string& path, HandlerFnNoBody fn);
  void put   (const std::string& path, HandlerFnWithBody fn);
  void del   (const std::string& path, HandlerFnNoBody fn);
  void patch (const std::string& path, HandlerFnNoBody fn);
  void patch (const std::string& path, HandlerFnWithBody fn);

  // ----------------------------
  // Group support
  // ----------------------------
  class Group {
   public:
    Group(RouterFactory* parent, std::string prefix);

    Group group(const std::string& child) const;

    void get   (const std::string& p, HandlerFnNoBody fn);
    void head  (const std::string& p, HandlerFnNoBody fn);
    void post  (const std::string& p, HandlerFnNoBody fn);
    void post  (const std::string& p, HandlerFnWithBody fn);
    void put   (const std::string& p, HandlerFnNoBody fn);
    void put   (const std::string& p, HandlerFnWithBody fn);
    void del   (const std::string& p, HandlerFnNoBody fn);
    void patch (const std::string& p, HandlerFnNoBody fn);
    void patch (const std::string& p, HandlerFnWithBody fn);

   private:
    static std::string normalize(std::string s);
    std::string join(const std::string& p) const;

    RouterFactory* parent_;
    std::string prefix_;
  };

  Group group(const std::string& prefix);

  // ----------------------------
  // Middleware system
  // ----------------------------
  void useBefore(std::function<bool(RouteContext&, Res&)> fn);
  void useAfter(std::function<void(const RouteContext&, Res&)> fn);

  // Built-in middlewares
  void useCORS();
  void useCompression();
  void useRequestIdLoggingAndMetrics(Metrics* m);

  Metrics* metrics() const { return metrics_; }

 private:
  // Trie structure for routes
  struct TrieNode {
    std::unordered_map<std::string, std::unique_ptr<TrieNode>> children;
    std::unique_ptr<TrieNode> paramChild;
    std::unique_ptr<TrieNode> wildcardChild;

    HandlerFnWithBody fnBody;
    HandlerFnNoBody fnNoBody;
    bool wantsBody = false;
    std::string paramName;
  };

  void insert(const std::string& method,
              const std::string& path,
              bool wantsBody,
              HandlerFnNoBody fnNoBody,
              HandlerFnWithBody fnWithBody);

  bool match(TrieNode* node,
             const std::vector<std::string>& parts,
             size_t i,
             std::unordered_map<std::string,std::string>& params,
             TrieNode*& out);

  std::unordered_map<std::string, std::unique_ptr<TrieNode>> methodRoots_;
  std::vector<Middleware> middlewares_;
  Metrics* metrics_;
};
