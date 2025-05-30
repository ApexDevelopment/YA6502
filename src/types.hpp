#pragma once

#include <stdint.h>

using Byte = uint8_t;
using Byte_S = int8_t;
using Word = uint16_t;

static constexpr Word CPU_FLAG_N = 0b10000000;
static constexpr Word CPU_FLAG_V = 0b01000000;
static constexpr Word CPU_FLAG_B = 0b00010000;
static constexpr Word CPU_FLAG_D = 0b00001000;
static constexpr Word CPU_FLAG_I = 0b00000100;
static constexpr Word CPU_FLAG_Z = 0b00000010;
static constexpr Word CPU_FLAG_C = 0b00000001;

static constexpr Word CPU_FLAG_UNUSED = 0b00100000;

static constexpr Byte CPU_UOP_NONE  = 0b0000;
static constexpr Byte CPU_UOP_FETCH = 0b0001;
static constexpr Byte CPU_UOP_WRITE = 0b0010;

static constexpr Byte CPU_ADDR_MODE_ABX     = 0b0000;
static constexpr Byte CPU_ADDR_MODE_ABY     = 0b0001;
static constexpr Byte CPU_ADDR_MODE_ACC     = 0b0010;
static constexpr Byte CPU_ADDR_MODE_ZPG     = 0b0100;
static constexpr Byte CPU_ADDR_MODE_ZPX     = 0b0101;
static constexpr Byte CPU_ADDR_MODE_ZPY     = 0b0110;
static constexpr Byte CPU_ADDR_MODE_IMM     = 0b1000;
static constexpr Byte CPU_ADDR_MODE_ABS     = 0b1001;
static constexpr Byte CPU_ADDR_MODE_ZPX_IND = 0b1101; // Seriously who designed this thing
static constexpr Byte CPU_ADDR_MODE_ZPY_IND = 0b1110;
static constexpr Byte CPU_ADDR_MODE_INVALID = 0b1111;

enum CPUStatus {
	CONTINUE = 0,
	HALT,
	INVALID
};