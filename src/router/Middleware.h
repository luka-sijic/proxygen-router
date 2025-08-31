#pragma once
#include <functional>
#include "RouteContext.h"
#include "Response.h"

struct Middleware {
  std::function<bool(RouteContext&, Res&)> before; // return true to short-circuit
  std::function<void(const RouteContext&, Res&)> after;
};
