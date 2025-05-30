#pragma once

#include <stdint.h>
#include "types.hpp"

constexpr uint8_t operator "" _b(unsigned long long x) {
	return static_cast<uint8_t>(x);
}

inline constexpr Word widen(Byte b) {
	return static_cast<Word>(b);
}

inline constexpr Byte lo(Word w) {
	return static_cast<Byte>(w & 0xFF_b);
}

inline constexpr Byte hi(Word w) {
	return static_cast<Byte>((w >> 8) & 0xFF_b);
}
