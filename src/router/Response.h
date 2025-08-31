#pragma once
#include <proxygen/httpserver/ResponseBuilder.h>
#include <nlohmann/json.hpp>
#include "RouteContext.h"

class Res {
 public:
  explicit Res(proxygen::ResponseBuilder& rb, RouteContext& ctx)
    : rb_(&rb), ctx_(&ctx) {}

  Res& status(uint16_t code, std::string msg="OK") { code_=code; msg_=std::move(msg); return *this; }
  Res& header(const std::string& k,const std::string& v){ headers_[k]=v; return *this; }
  Res& text(const std::string& s,uint16_t code=200){ code_=code; headers_["content-type"]="text/plain"; body_=s; return *this; }
  Res& json(const nlohmann::json& j,uint16_t code=200,bool pretty=false){ code_=code; headers_["content-type"]="application/json"; body_=j.dump(pretty?2:-1); return *this; }

  void send() {
    rb_->status(code_, msg_);
    for (auto& kv : headers_) rb_->header(kv.first, kv.second);
    rb_->body(body_);
    rb_->sendWithEOM();
  }

  uint16_t& code(){return code_;} std::string& body(){return body_;}
  std::unordered_map<std::string,std::string>& headers(){return headers_;}
  const RouteContext& ctx() const {return *ctx_;}

 private:
  proxygen::ResponseBuilder* rb_;
  RouteContext* ctx_;
  uint16_t code_{200}; std::string msg_{"OK"};
  std::unordered_map<std::string,std::string> headers_;
  std::string body_;
};
