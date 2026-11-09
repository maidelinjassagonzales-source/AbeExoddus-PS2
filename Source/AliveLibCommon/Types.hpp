#pragma once

#include <stdint.h>

using u8 = uint8_t;
using s8 = signed char;
using char_type = char;

using u16 = uint16_t;
using s16 = int16_t;

// PS2 newlib defines int32_t as long; the engine relies on s32 being int (printf, std::min, callbacks).
static_assert(sizeof(int) == 4, "s32 must be 32 bits");
using u32 = unsigned int;
using s32 = int;

using f32 = float;
using f64 = double;

using u64 = uint64_t;
using s64 = int64_t;

using Bool32 = long;
