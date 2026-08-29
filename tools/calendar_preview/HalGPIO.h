#pragma once
#include <cstdint>
class HalGPIO {
 public:
  bool deviceIsX3() const { return true; }
};
extern HalGPIO gpio;
