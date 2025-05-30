#pragma once

#include <stdint.h>
#include <string>
#include <stdexcept>
#include <cctype>
#include "types.hpp"

inline int parse_numeric_literal(const std::string& str) {
	std::size_t offset = 0;
	int base = 10;

	// Skip leading whitespace
	while (offset < str.size() && std::isspace(str[offset])) {
		++offset;
	}

	// Prefix detection
	if (str.compare(offset, 2, "0x") == 0 || str[offset] == '$') {
		base = 16;
		offset += static_cast<std::size_t>((str[offset] == '$') ? 1 : 2); // Shut up compiler
	} else if (str.compare(offset, 2, "0b") == 0) {
		base = 2;
		offset += 2;
	}

	std::string clean = str.substr(offset);
	return std::stoi(clean, nullptr, base);
}

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

inline constexpr Word make_address(Byte b_lo, Byte b_hi) {
	return static_cast<Word>(widen(b_lo) | (widen(b_hi) << 8));
}