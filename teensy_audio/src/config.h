#pragma once

#include <stdint.h>

namespace cfg {

// Application units, 0..100. Kept low because a TPA3255 will be downstream.
// Applied as digital channel gain. 100 is full scale into the line outputs.
constexpr uint8_t kDefaultMasterVolume = 30;
constexpr uint8_t kDefaultSirenGain = 50;
constexpr uint8_t kDefaultRumblerGain = 30;

}  // namespace cfg
