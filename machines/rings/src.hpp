#pragma once

#include <stdint.h>

#include "tables.hpp"

namespace rings {
namespace src {

static const uint32_t kPhaseCount = 147u;
static const uint32_t kPhaseStep = 13u;  // 160 mod 147.
static const uint32_t kTapCount = 256u;
static const uint32_t kHistoryWords = 512u;
static const int32_t kHalfscaleLimit = 1073741823;

struct Q31SrcState {
  int32_t history[kHistoryWords];
  uint32_t write_index;
  uint32_t source_index;
  uint32_t next_base;
  uint32_t phase;
};

struct Q31SrcOutput {
  int64_t raw_rounded;
  int32_t clamped;
};

enum PushResult {
  kNoOutput,
  kOutput,
  kInvalidPhase
};

// Bounded unsigned 16x16 limbs avoid a target-dependent 32x32->64 helper.
// Inputs are signed 32-bit magnitudes, so the full product is at most 2^62.
static inline int64_t MultiplySigned32(int32_t left, int32_t right) {
  const bool negative = (left < 0) != (right < 0);
  const uint32_t a = left < 0 ? 0u - static_cast<uint32_t>(left)
                              : static_cast<uint32_t>(left);
  const uint32_t b = right < 0 ? 0u - static_cast<uint32_t>(right)
                              : static_cast<uint32_t>(right);
  const uint32_t a0 = a & 0xffffu;
  const uint32_t a1 = a >> 16;
  const uint32_t b0 = b & 0xffffu;
  const uint32_t b1 = b >> 16;
  const uint32_t p00 = a0 * b0;
  const uint32_t p01 = a0 * b1;
  const uint32_t p10 = a1 * b0;
  const uint32_t p11 = a1 * b1;
  const uint32_t middle = (p00 >> 16) + (p01 & 0xffffu) + (p10 & 0xffffu);
  const uint32_t low = ((middle & 0xffffu) << 16) | (p00 & 0xffffu);
  const uint32_t high = p11 + (p01 >> 16) + (p10 >> 16) + (middle >> 16);
  const uint64_t magnitude = (static_cast<uint64_t>(high) << 32) | low;
  const int64_t signed_magnitude = static_cast<int64_t>(magnitude);
  return negative ? -signed_magnitude : signed_magnitude;
}

// Round a signed Q62 accumulator back to Q31 without signed shifts or
// negating INT64_MIN. Valid filter accumulators are bounded well inside int64.
static inline int64_t RoundQ31Accumulator(int64_t accumulator) {
  const bool negative = accumulator < 0;
  const uint64_t magnitude = negative
      ? 0u - static_cast<uint64_t>(accumulator)
      : static_cast<uint64_t>(accumulator);
  uint64_t rounded = magnitude >> 31;
  if ((magnitude & 0x7fffffffu) >= 0x40000000u) {
    ++rounded;
  }
  const int64_t signed_rounded = static_cast<int64_t>(rounded);
  return negative ? -signed_rounded : signed_rounded;
}

// Keep the ABI halfscale clamp in the wide domain before narrowing to int32.
static inline int32_t ClampQ31ToHalfscale(int64_t value) {
  if (value > kHalfscaleLimit) {
    return kHalfscaleLimit;
  }
  if (value < -kHalfscaleLimit) {
    return -kHalfscaleLimit;
  }
  return static_cast<int32_t>(value);
}

// Push one already-halfscale-clamped 48-kHz source sample. The table accessor
// validates the bounded rational phase and records an impossible index in the
// callback-owned sticky stats; a valid output is always strictly historical.
static inline PushResult PushQ31Src(Q31SrcState* state, int32_t input,
                                    dsp::NumericStats& stats,
                                    Q31SrcOutput* output) {
  state->history[state->write_index] = input;
  state->write_index = (state->write_index + 1u) & (kHistoryWords - 1u);

  const uint32_t current_source = state->source_index;
  state->source_index = current_source + 1u;  // intentional unsigned wrap.
  if (state->next_base != current_source) {
    return kNoOutput;
  }

  const int32_t* coefficients = 0;
  bool reverse_taps = false;
  if (!dsp::src_coefficient_row(state->phase, &coefficients, &reverse_taps, stats)) {
    return kInvalidPhase;
  }
  const uint32_t reverse_tap_mask = reverse_taps ? 0xffu : 0u;
  int64_t accumulator = 0;
  for (uint32_t tap = 0; tap < kTapCount; ++tap) {
    const uint32_t age = 287u - tap;
    const uint32_t sample_index =
        (state->write_index - 1u - age) & (kHistoryWords - 1u);
    const int32_t sample = state->history[sample_index];
    const int32_t coefficient = coefficients[tap ^ reverse_tap_mask];
    accumulator += MultiplySigned32(sample, coefficient);
  }

  output->raw_rounded = RoundQ31Accumulator(accumulator);
  output->clamped = ClampQ31ToHalfscale(output->raw_rounded);

  const uint32_t advanced_phase = state->phase + kPhaseStep;
  state->next_base += 1u + (advanced_phase >= kPhaseCount ? 1u : 0u);
  state->phase = advanced_phase >= kPhaseCount
      ? advanced_phase - kPhaseCount
      : advanced_phase;
  return kOutput;
}

}  // namespace src
}  // namespace rings
