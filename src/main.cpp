#include <folly/init/Init.h>
#include <proxygen/httpserver/HTTPServer.h>

#include <thread>

#include "db/DB.h"
#include "db/UserService.h"
#include "dotenv.hpp"
#include "router/Router.h"

int main(int argc, char *argv[]) {
  folly::Init init(&argc, &argv);
  dotenv::load(".env", /*overwrite=*/false, /*expand ${VAR}*/ true);
  const char *url = std::getenv("DATABASE_URL");

  DBPool db(url, 8);
  UserService userService(db);

  auto router = std::make_unique<RouterFactory>();

  // --- middlewares: CORS, compression, request-id+metrics

  // Enable built-in middlewares
  router->useCORS();
  router->useCompression();

  static Metrics metrics;
  router->useRequestIdLoggingAndMetrics(&metrics);

  static Metrics M; // global/simple
  router->useRequestIdLoggingAndMetrics(&M);

  // --- routes
  router->get("/ping", [](Res &res) { res.text("pong\n"); });

  // Prometheus /metrics
  router->get("/metrics", [](Res &res) {
    res.header("content-type", "text/plain; version=0.0.4").text(M.render());
  });

  auto api = router->group("/api/v1");
  api.get("/users/:id", [&](Res &res) {
    int id = std::stoi(res.ctx().param("id"));
    auto user = userService.getUserById(id);
    res.json(user);
  });
  api.post("/register", [&](const std::string &body, Res &res) {
    auto j = nlohmann::json::parse(body);
    auto result =
        userService.registerUser(j["username"], j["password"], j["email"]);
    if (result != "success") {
      res.json("failed");
    }
    res.json("success");
  });
  api.get("/hello", [](Res &res) { res.json({{"msg", "hello"}}, 200, true); });
  api.post("/echo", [](const std::string &body, Res &res) {
    res.json({{"you_posted", body}});
  });

  // --- server
  proxygen::HTTPServer::IPConfig ip{folly::SocketAddress("0.0.0.0", 8080, true),
                                    proxygen::HTTPServer::Protocol::HTTP};
  proxygen::HTTPServerOptions opt;
  opt.threads = std::thread::hardware_concurrency();
  opt.handlerFactories =
      proxygen::RequestHandlerChain().addThen(std::move(router)).build();

  proxygen::HTTPServer srv(std::move(opt));
  srv.bind({ip});
  std::cout << "🚀 Server running on http://127.0.0.1:8080\n";
  srv.start(); // blocking
}
