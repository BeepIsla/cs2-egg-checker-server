#pragma once

#include <cstdint>

inline constexpr uint32_t CONNECTIONLESS_HEADER = 0xffffffff;

inline constexpr uint8_t A2S_GETCHALLENGE = 'q';
inline constexpr uint8_t S2C_CHALLENGE    = 'A';

inline constexpr uint8_t C2S_CONNECT    = 'k';
inline constexpr uint8_t S2C_CONNECTION = 'B';
