// USB serial link: one JSON request per line in, NDJSON responses/events out.
#pragma once

#include <string>

#include "router.h"

class SerialLink {
 public:
  void begin(Router& router);
  void loop();

 private:
  static constexpr size_t kMaxLine = 1024;

  Router* router_ = nullptr;
  std::string line_;
  bool overflow_ = false;
};
