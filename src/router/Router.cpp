#include "Router.h"
#include "PathPattern.h"
#include "Response.h"
#include "Middleware.h"
#include "Metrics.h"
#include "Compression.h"

#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <folly/io/async/EventBase.h>
#include <chrono>
#include <random>
#include <numeric>

// Forward declare RouterHandler if not split
class RouterHandler : public proxygen::RequestHandler {
 public:
  RouterHandler(RouterFactory::HandlerFnWithBody fn,
                std::vector<Middleware> mws,
                RouteContext ctx)
      : fnBody_(std::move(fn)), middlewares_(std::move(mws)), ctx_(std::move(ctx)) {}

  RouterHandler(RouterFactory::HandlerFnNoBody fn,
                std::vector<Middleware> mws,
                RouteContext ctx)
      : fnNoBody_(std::move(fn)), middlewares_(std::move(mws)), ctx_(std::move(ctx)) {}

  void onRequest(std::unique_ptr<proxygen::HTTPMessage>) noexcept override {}

  void onBody(std::unique_ptr<folly::IOBuf> b) noexcept override {
    if (b) body_.append(reinterpret_cast<const char*>(b->data()), b->length());
  }

  void onEOM() noexcept override {
    proxygen::ResponseBuilder rb(downstream_);
    Res res(rb, ctx_);

    // before middlewares
    for (auto& mw : middlewares_) {
      if (mw.before && mw.before(ctx_, res)) {
        res.send();
        return;
      }
    }

    // handler
    if (fnBody_)   fnBody_(body_, res);
    if (fnNoBody_) fnNoBody_(res);

    // after middlewares
    for (auto& mw : middlewares_) {
      if (mw.after) mw.after(ctx_, res);
    }

    res.send();
  }

  void onUpgrade(proxygen::UpgradeProtocol) noexcept override {}
  void onError(proxygen::ProxygenError) noexcept override { delete this; }
  void requestComplete() noexcept override { delete this; }

  void setParams(std::unordered_map<std::string,std::string> p) {
    ctx_.params = std::move(p);
  }

 private:
  RouterFactory::HandlerFnWithBody fnBody_;
  RouterFactory::HandlerFnNoBody fnNoBody_;
  std::vector<Middleware> middlewares_;
  RouteContext ctx_;
  std::string body_;
};

// ============================================================================
// RouterFactory implementation
// ============================================================================

RouterFactory::RouterFactory() : metrics_(nullptr) {}
void RouterFactory::onServerStart(folly::EventBase*) noexcept {}
void RouterFactory::onServerStop() noexcept {}

// Insert a route into the Trie
void RouterFactory::insert(const std::string& method,
                           const std::string& path,
                           bool wantsBody,
                           HandlerFnNoBody fnNoBody,
                           HandlerFnWithBody fnWithBody) {
  if (!methodRoots_.count(method)) {
    methodRoots_[method] = std::make_unique<TrieNode>();
  }
  TrieNode* node = methodRoots_[method].get();

  auto parts = splitPath(path);
  for (size_t i=0; i<parts.size(); i++) {
    auto& seg = parts[i];
    if (!seg.empty() && seg[0]==':') {
      if (!node->paramChild) node->paramChild = std::make_unique<TrieNode>();
      node = node->paramChild.get();
      node->paramName = seg.substr(1);
    } else if (!seg.empty() && seg[0]=='*') {
      if (!node->wildcardChild) node->wildcardChild = std::make_unique<TrieNode>();
      node = node->wildcardChild.get();
      node->paramName = seg.substr(1);
      break; // wildcard consumes rest
    } else {
      if (!node->children.count(seg))
        node->children[seg] = std::make_unique<TrieNode>();
      node = node->children[seg].get();
    }
  }
  node->wantsBody = wantsBody;
  node->fnNoBody = std::move(fnNoBody);
  node->fnBody = std::move(fnWithBody);
}

// Match against Trie
bool RouterFactory::match(TrieNode* node,
                          const std::vector<std::string>& parts,
                          size_t i,
                          std::unordered_map<std::string,std::string>& params,
                          TrieNode*& out) {
  if (i == parts.size()) {
    if (node->fnNoBody || node->fnBody) { out = node; return true; }
    return false;
  }
  auto& seg = parts[i];

  // literal match
  if (node->children.count(seg)) {
    if (match(node->children[seg].get(), parts, i+1, params, out)) return true;
  }
  // param match
  if (node->paramChild) {
    params[node->paramChild->paramName] = seg;
    if (match(node->paramChild.get(), parts, i+1, params, out)) return true;
    params.erase(node->paramChild->paramName);
  }
  // wildcard
  if (node->wildcardChild) {
    params[node->wildcardChild->paramName] =
      std::accumulate(parts.begin()+i, parts.end(), std::string(),
        [](auto& acc,const auto& s){ return acc.empty()?s:acc+"/"+s; });
    out = node->wildcardChild.get();
    return true;
  }
  return false;
}

// Handle new requests
proxygen::RequestHandler* RouterFactory::onRequest(
    proxygen::RequestHandler*, proxygen::HTTPMessage* msg) noexcept {

  RouteContext ctx;
  ctx.method = msg->getMethodString();
  ctx.path   = stripQuery(msg->getPath());
  ctx.start  = std::chrono::steady_clock::now();
  ctx.requestId = [] {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    uint64_t a=rng(), b=rng();
    char buf[33]; snprintf(buf,sizeof(buf),"%016llx%016llx",
                           (unsigned long long)a,(unsigned long long)b);
    return std::string(buf);
  }();

  // headers
  msg->getHeaders().forEach([&](const std::string& name, const std::string& value) {
    std::string k = name;
    for (auto& c: k) c = char(::tolower(c));
    ctx.reqHeaders[k] = value;
  });

  const auto parts = splitPath(ctx.path);

  auto it = methodRoots_.find(ctx.method);
  if (it != methodRoots_.end()) {
    TrieNode* matched=nullptr;
    std::unordered_map<std::string,std::string> params;
    if (match(it->second.get(), parts, 0, params, matched)) {
      auto h = matched->wantsBody
        ? new RouterHandler(matched->fnBody, middlewares_, ctx)
        : new RouterHandler(matched->fnNoBody, middlewares_, ctx);
      h->setParams(std::move(params));
      return h;
    }
  }

  // fallback 404
  return new RouterHandler(
    [](Res& res){ res.status(404,"Not Found").text("no route\n"); },
    middlewares_,
    ctx
  );
}

// ============================================================================
// Route registration wrappers
// ============================================================================

void RouterFactory::get(const std::string& path, HandlerFnNoBody fn) {
  insert("GET", path, false, std::move(fn), {});
}
void RouterFactory::head(const std::string& path, HandlerFnNoBody fn) {
  insert("HEAD", path, false, std::move(fn), {});
}
void RouterFactory::post(const std::string& path, HandlerFnNoBody fn) {
  insert("POST", path, false, std::move(fn), {});
}
void RouterFactory::post(const std::string& path, HandlerFnWithBody fn) {
  insert("POST", path, true, {}, std::move(fn));
}
void RouterFactory::put(const std::string& path, HandlerFnNoBody fn) {
  insert("PUT", path, false, std::move(fn), {});
}
void RouterFactory::put(const std::string& path, HandlerFnWithBody fn) {
  insert("PUT", path, true, {}, std::move(fn));
}
void RouterFactory::del(const std::string& path, HandlerFnNoBody fn) {
  insert("DELETE", path, false, std::move(fn), {});
}
void RouterFactory::patch(const std::string& path, HandlerFnNoBody fn) {
  insert("PATCH", path, false, std::move(fn), {});
}
void RouterFactory::patch(const std::string& path, HandlerFnWithBody fn) {
  insert("PATCH", path, true, {}, std::move(fn));
}

// ============================================================================
// Middleware registration
// ============================================================================

void RouterFactory::useBefore(std::function<bool(RouteContext&, Res&)> fn) {
  middlewares_.push_back({std::move(fn), {}});
}
void RouterFactory::useAfter(std::function<void(const RouteContext&, Res&)> fn) {
  middlewares_.push_back({{}, std::move(fn)});
}

// ============================================================================
// Built-in middlewares
// ============================================================================

void RouterFactory::useCORS() {
  useBefore([](RouteContext& ctx, Res& res)->bool {
    if (ctx.method=="OPTIONS" && !ctx.header("access-control-request-method").empty()) {
      res.status(204,"No Content");
      res.header("access-control-allow-origin","*");
      res.header("access-control-allow-methods","GET,POST,PUT,PATCH,DELETE,OPTIONS");
      res.header("access-control-allow-headers","Content-Type,Authorization,X-Requested-With");
      return true;
    }
    return false;
  });
  useAfter([](const RouteContext&, Res& res){
    res.header("access-control-allow-origin","*");
  });
}

void RouterFactory::useCompression() {
  useAfter([](const RouteContext& ctx, Res& res){
    auto ae = ctx.header("accept-encoding");
    if (res.body().empty()) return;
    if (ae.find("br")!=std::string::npos) {
      std::string out; if (brotliCompress(res.body(), out)) {
        res.headers()["content-encoding"] = "br";
        res.body().swap(out);
        return;
      }
    }
    if (ae.find("gzip")!=std::string::npos) {
      std::string out; if (gzipCompress(res.body(), out)) {
        res.headers()["content-encoding"] = "gzip";
        res.body().swap(out);
      }
    }
  });
}

void RouterFactory::useRequestIdLoggingAndMetrics(Metrics* m) {
  metrics_ = m;
  useBefore([this](RouteContext& ctx, Res& res){
    if (!ctx.requestId.empty()) res.header("x-request-id", ctx.requestId);
    if (metrics_) metrics_->in_flight++;
    return false;
  });
  useAfter([this](const RouteContext& ctx, Res& res){
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double,std::milli>(end-ctx.start).count();
    if (metrics_) {
      metrics_->record(ctx.method+":"+ctx.path, ms, res.code()>=500);
      metrics_->in_flight--;
    }
  });
}

// ============================================================================
// RouterFactory::Group implementation
// ============================================================================

RouterFactory::Group::Group(RouterFactory* parent, std::string prefix)
    : parent_(parent), prefix_(normalize(std::move(prefix))) {}

RouterFactory::Group RouterFactory::Group::group(const std::string& child) const {
  return Group(parent_, join(child));
}

void RouterFactory::Group::get(const std::string& p, HandlerFnNoBody fn) {
  parent_->get(join(p), std::move(fn));
}
void RouterFactory::Group::head(const std::string& p, HandlerFnNoBody fn) {
  parent_->head(join(p), std::move(fn));
}
void RouterFactory::Group::post(const std::string& p, HandlerFnNoBody fn) {
  parent_->post(join(p), std::move(fn));
}
void RouterFactory::Group::post(const std::string& p, HandlerFnWithBody fn) {
  parent_->post(join(p), std::move(fn));
}
void RouterFactory::Group::put(const std::string& p, HandlerFnNoBody fn) {
  parent_->put(join(p), std::move(fn));
}
void RouterFactory::Group::put(const std::string& p, HandlerFnWithBody fn) {
  parent_->put(join(p), std::move(fn));
}
void RouterFactory::Group::del(const std::string& p, HandlerFnNoBody fn) {
  parent_->del(join(p), std::move(fn));
}
void RouterFactory::Group::patch(const std::string& p, HandlerFnNoBody fn) {
  parent_->patch(join(p), std::move(fn));
}
void RouterFactory::Group::patch(const std::string& p, HandlerFnWithBody fn) {
  parent_->patch(join(p), std::move(fn));
}

// Helpers
std::string RouterFactory::Group::normalize(std::string s) {
  if (s.empty() || s[0] != '/') s = "/" + s;
  if (s.size() > 1 && s.back() == '/') s.pop_back();
  return s;
}

std::string RouterFactory::Group::join(const std::string& p) const {
  if (p.empty() || p[0] != '/') return prefix_ + "/" + p;
  return prefix_ + p;
}

RouterFactory::Group RouterFactory::group(const std::string& prefix) {
  return Group(this, prefix);
}
