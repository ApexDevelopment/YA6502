#pragma once

#include <iostream>
#include <bitset>
#include <type_traits>
#include <string>

struct bin_t { };
constexpr bin_t bin;

struct binary_stream {
	std::ostream& os;

	// Only handle integral types with this overload
	template<typename T>
	typename std::enable_if<std::is_integral<T>::value, binary_stream&>::type
	operator<<(T value) {
		if (value == 0) {
			os << '0';
		} else {
			std::bitset<sizeof(T) * 8> bits(static_cast<unsigned long long>(value));
			auto str = bits.to_string();
			os << str.substr(str.find('1'));
		}
		return *this;
	}

	binary_stream& operator<<(std::ostream& (*manip)(std::ostream&)) {
		os << manip;
		return *this;
	}

	// Forward strings and other non-integral types
	binary_stream& operator<<(const std::string& s) {
		os << s;
		return *this;
	}
	binary_stream& operator<<(const char* s) {
		os << s;
		return *this;
	}
	binary_stream& operator<<(char c) {
		os << c;
		return *this;
	}
};

inline binary_stream operator<<(std::ostream& os, bin_t) {
	return {os};
}
