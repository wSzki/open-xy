#pragma once

#include <stdint.h>
#include <limits.h>

namespace rings {
namespace dsp {

using Audio = int32_t;       // Signed Q25 audio and history state.
using Wide = int64_t;        // Signed Q25 sum/difference domain, not raw products.
using Unit = uint32_t;       // Unsigned Q31 [0, 1], with exact unity 0x80000000.
using Gain = int32_t;        // Signed Q29 [-4, 4).
using Frequency = uint64_t;  // Unsigned Q40 normalized 48-kHz base.
using Delay = int64_t;       // Signed Q20 delay coordinates.

static const Unit kUnitOne = UINT32_C(0x80000000);
static const uint32_t kGOneQ24 = UINT32_C(0x01000000);
static const uint32_t kROneQ30 = UINT32_C(0x40000000);
static const uint32_t kRTwoQ30 = UINT32_C(0x80000000);

struct Patch {
  Unit structure;
  Unit brightness;
  Unit damping;
  Unit position;
};

// Plain zero-initializable sticky counters. Increment helpers saturate rather
// than wrapping, so a long-running callback cannot erase earlier diagnostics.
struct NumericStats {
  uint32_t internal_saturation;
  uint32_t invalid_division;
  uint32_t invalid_range;
  uint32_t invalid_index;
};

inline void increment_sticky(uint32_t* counter) {
  if (*counter != UINT32_MAX) {
    ++*counter;
  }
}

inline void note_saturation(NumericStats& stats) {
  increment_sticky(&stats.internal_saturation);
}

inline void note_invalid_division(NumericStats& stats) {
  increment_sticky(&stats.invalid_division);
}

inline void note_invalid_range(NumericStats& stats) {
  increment_sticky(&stats.invalid_range);
}

inline void note_invalid_index(NumericStats& stats) {
  increment_sticky(&stats.invalid_index);
}

// Nearest integer after a power-of-two scale change; exact half cases round
// away from zero. Magnitude extraction is defined for INT64_MIN and no signed
// right shift or signed negation of the minimum value is used.
inline int64_t round_shift(int64_t value, unsigned shift, NumericStats& stats) {
  if (shift > 63u) {
    note_invalid_range(stats);
    return 0;
  }
  if (shift == 0u) {
    return value;
  }

  const bool negative = value < 0;
  const uint64_t magnitude = negative
      ? static_cast<uint64_t>(-(value + 1)) + UINT64_C(1)
      : static_cast<uint64_t>(value);
  const uint64_t mask = UINT64_MAX >> (64u - shift);
  uint64_t quotient = magnitude >> shift;
  if ((magnitude & mask) >= (UINT64_C(1) << (shift - 1u))) {
    ++quotient;
  }

  if (!negative) {
    return static_cast<int64_t>(quotient);
  }
  if (quotient == (UINT64_C(1) << 63)) {
    return INT64_MIN;
  }
  return -static_cast<int64_t>(quotient);
}

inline int32_t narrow_i32(int64_t value, NumericStats& stats) {
  if (value > INT32_MAX) {
    note_saturation(stats);
    return INT32_MAX;
  }
  if (value < INT32_MIN) {
    note_saturation(stats);
    return INT32_MIN;
  }
  return static_cast<int32_t>(value);
}

inline int64_t add_wide(int64_t left, int64_t right, NumericStats& stats) {
  if (right > 0 && left > INT64_MAX - right) {
    note_saturation(stats);
    return INT64_MAX;
  }
  if (right < 0 && left < INT64_MIN - right) {
    note_saturation(stats);
    return INT64_MIN;
  }
  return left + right;
}

inline int64_t sub_wide(int64_t left, int64_t right, NumericStats& stats) {
  if (right < 0 && left > INT64_MAX + right) {
    note_saturation(stats);
    return INT64_MAX;
  }
  if (right > 0 && left < INT64_MIN + right) {
    note_saturation(stats);
    return INT64_MIN;
  }
  return left - right;
}

// A signed 32x32 product is bounded by 2^62. The result remains in int64 so
// callers can keep wide sums until their deliberate narrowing point.
inline int64_t mul_q_wide(int32_t left, int32_t right, unsigned fractional_bits,
                          NumericStats& stats) {
  if (fractional_bits > 63u) {
    note_invalid_range(stats);
    return 0;
  }
  const int64_t product = static_cast<int64_t>(left) *
                          static_cast<int64_t>(right);
  return round_shift(product, fractional_bits, stats);
}

inline int32_t mul_q(int32_t left, int32_t right, unsigned fractional_bits,
                     NumericStats& stats) {
  return narrow_i32(mul_q_wide(left, right, fractional_bits, stats), stats);
}

inline Audio scale_audio_unit(Audio value, Unit scale, NumericStats& stats) {
  if (scale > kUnitOne) {
    note_invalid_range(stats);
    scale = kUnitOne;
  }
  const int64_t product = static_cast<int64_t>(value) *
                          static_cast<int64_t>(scale);
  return narrow_i32(round_shift(product, 31u, stats), stats);
}

inline Audio scale_audio_gain(Audio value, Gain scale, NumericStats& stats) {
  const int64_t product = static_cast<int64_t>(value) *
                          static_cast<int64_t>(scale);
  return narrow_i32(round_shift(product, 29u, stats), stats);
}

inline Unit multiply_unit(Unit left, Unit right, NumericStats& stats) {
  if (left > kUnitOne) {
    note_invalid_range(stats);
    left = kUnitOne;
  }
  if (right > kUnitOne) {
    note_invalid_range(stats);
    right = kUnitOne;
  }
  const int64_t product = static_cast<int64_t>(left) *
                          static_cast<int64_t>(right);
  return static_cast<Unit>(round_shift(product, 31u, stats));
}

// Rounded unsigned 64-bit division by restoring long division. The remainder
// update compares against denominator-remainder before doubling, so neither
// the numerator nor an intermediate remainder needs an overflowing shift.
inline uint64_t divide_u64_round(uint64_t numerator, uint64_t denominator,
                                 NumericStats& stats) {
  if (denominator == 0u) {
    note_invalid_division(stats);
    return 0u;
  }
  if (numerator == 0u) {
    return 0u;
  }
  uint64_t quotient = 0u;
  uint64_t remainder = 0u;
  for (int bit = 63; bit >= 0; --bit) {
    const uint64_t input_bit =
        (numerator >> static_cast<unsigned>(bit)) & UINT64_C(1);
    const uint64_t complement = denominator - remainder;
    bool quotient_bit = false;
    if (remainder >= complement) {
      remainder -= complement;
      remainder += input_bit;
      quotient_bit = true;
    } else {
      remainder += remainder;
      if (input_bit != 0u) {
        if (remainder == denominator - 1u) {
          remainder = 0u;
          quotient_bit = true;
        } else {
          ++remainder;
        }
      }
    }
    quotient = (quotient << 1u) | (quotient_bit ? 1u : 0u);
  }

  if (remainder >= denominator - remainder) {
    if (quotient == UINT64_MAX) {
      note_saturation(stats);
      return UINT64_MAX;
    }
    ++quotient;
  }
  return quotient;
}

// Convert normalized Q40 frequency to source-sample delay in Q20.
inline Delay frequency_reciprocal_q20(Frequency frequency,
                                      NumericStats& stats) {
  const uint64_t quotient = divide_u64_round(UINT64_C(1) << 60u, frequency,
                                              stats);
  return static_cast<Delay>(quotient);
}

// A bounded restoring fractional divider. It computes round(numerator /
// denominator * 2^31), without shifting the numerator or dividing a 64-bit
// value. This helper is specifically for normalized [0, 1] ratios.
inline Unit ratio_q31(uint64_t numerator, uint64_t denominator,
                      NumericStats& stats) {
  if (denominator == 0u) {
    note_invalid_division(stats);
    return 0u;
  }
  if (numerator > denominator) {
    note_invalid_range(stats);
    return kUnitOne;
  }
  if (numerator == 0u) {
    return 0u;
  }
  if (numerator == denominator) {
    return kUnitOne;
  }

  uint64_t remainder = numerator;
  uint32_t quotient = 0u;
  for (unsigned bit = 0u; bit < 31u; ++bit) {
    quotient <<= 1u;
    const uint64_t complement = denominator - remainder;
    if (remainder >= complement) {
      remainder -= complement;
      quotient |= 1u;
    } else {
      remainder += remainder;
    }
  }
  if (remainder >= denominator - remainder) {
    ++quotient;
  }
  return quotient;
}

// Compute normalized state-variable filter coefficients from g in Q24 and r
// in Q30. Valid domain: 0 <= g < 7, 0 <= r <= 2. The polynomial is evaluated
// in Q48 and stays below 2^54; each output coefficient is a bounded Q31 ratio.
inline void svf_coefficients(uint32_t g_q24, uint32_t r_q30, Unit& h,
                             Unit& a2, Unit& a3, NumericStats& stats) {
  const uint32_t max_g_q24 = UINT32_C(7) * kGOneQ24;
  if (g_q24 >= max_g_q24 || r_q30 > kRTwoQ30) {
    note_invalid_range(stats);
    h = 0u;
    a2 = 0u;
    a3 = 0u;
    return;
  }

  const uint32_t r_q24 = static_cast<uint32_t>(
      round_shift(static_cast<int64_t>(r_q30), 6u, stats));
  const uint32_t g_plus_r = g_q24 + r_q24;
  const uint64_t gg_plus_gr =
      static_cast<uint64_t>(g_q24) * g_plus_r;
  const uint64_t one_q48 = UINT64_C(1) << 48;
  const uint64_t denominator = one_q48 + gg_plus_gr;
  h = ratio_q31(one_q48, denominator, stats);
  a2 = ratio_q31(static_cast<uint64_t>(g_q24) << 24u, denominator, stats);
  a3 = ratio_q31(static_cast<uint64_t>(g_q24) * g_q24, denominator, stats);
}

// The SDK pitch increment is Q0.32 at 44.1 kHz. Preserve its low bits while
// mapping to the internal 48-kHz Q40 carrier using nearest(raw * 1176 / 5).
inline Frequency sdk_frequency(uint32_t raw_increment, NumericStats&) {
  const uint64_t quotient = static_cast<uint64_t>(raw_increment) * 235u +
                            static_cast<uint64_t>(raw_increment / 5u);
  const uint32_t remainder = raw_increment % 5u;
  return quotient + (remainder >= 3u ? 1u : 0u);
}

// Per-context LCG. Unsigned overflow is defined modulo 2^32.
inline uint32_t lcg_next(uint32_t& state) {
  state = state * UINT32_C(1664525) + UINT32_C(1013904223);
  return state;
}

// Map a full unsigned word to signed Q25 white noise in [-1, 1] after rounding.
inline Audio noise_q25(uint32_t word, NumericStats& stats) {
  const int64_t centered = static_cast<int64_t>(word) - INT64_C(0x80000000);
  return narrow_i32(round_shift(centered, 6u, stats), stats);
}

}  // namespace dsp
}  // namespace rings
