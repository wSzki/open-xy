// Copyright 2015 Emilie Gillet.
// Manual fixed-point rewrite of Mutable Instruments Rings and stmlib DSP algorithm flow.
// See vendor/LICENSE and vendor/VENDOR.json for the MIT notice, pinned source commits,
// and source hashes.

#include "model.hpp"

#include "tables.hpp"

#include <cstring>

namespace rings {
namespace dsp {
namespace {

enum TangentApproximation {
  kTangentDirty,
  kTangentFast,
  kTangentAccurate
};

static const int32_t kAudioOne = INT32_C(1) << 25;
static const int32_t kAudioHalf = INT32_C(1) << 24;
static const int32_t kGainOne = INT32_C(1) << 29;
static const int32_t kPiQ16 = 205887;
static const uint32_t kQ24One = UINT32_C(1) << 24;
static const uint32_t kQ24Nyquist499 = (UINT32_C(499) * kQ24One) / UINT32_C(1000);
static const uint64_t kFrequencyOne = UINT64_C(1) << 40;
static const uint64_t kFrequencyNyquist499 =
    (kFrequencyOne * UINT64_C(499)) / UINT64_C(1000);
static const Delay kDelayMinimum = INT64_C(4) << 20;
static const Delay kDelayMaximum = INT64_C(2044) << 20;
static const Unit kUnitHalf = UINT32_C(0x40000000);
static const Unit kUnitThreeQuarter = UINT32_C(1610612736);
static const Unit kUnitEightyFivePercent = UINT32_C(1825361101);
static const Unit kUnitTwentyPercent = UINT32_C(429496730);
static const Unit kUnitFifteenPercent = UINT32_C(322122547);
static const Unit kUnitTenPercent = UINT32_C(214748365);
static const Unit kUnitNinetyFivePercent = UINT32_C(2040109466);
static const Unit kUnitNinetyEightPercent = UINT32_C(2104533975);
static const Unit kUnitNinetyPercent = UINT32_C(1932735283);
static const Unit kUnitFortySevenFivePercent = UINT32_C(1020054733);
static const Unit kUnitSixtyOneEightPercent = UINT32_C(1327142340);
static const Unit kUnitOnePercent = UINT32_C(21474836);
static const Unit kUnitTwoAndHalfPercent = UINT32_C(53687091);
static const Gain kGainEightyPercent = INT32_C(429496730);
static const uint32_t kFourDecadesScale = UINT32_C(500);
static const uint32_t kQ31SlopeAttack = UINT32_C(107374182);
static const uint32_t kQ31SlopeRelease = UINT32_C(42950);
static const Unit kDcPole = UINT32_C(2146588863);
static const uint32_t kLfoFrequencyQ29[kStringBankVoices] = {
    UINT32_C(134218), UINT32_C(107374), UINT32_C(93952), UINT32_C(61740),
    UINT32_C(56640), UINT32_C(53687), UINT32_C(45902), UINT32_C(0)};
static const int32_t kSympatheticNotesQ16[9] = {
    0, -786432, -460033, 0, 460033, 786432, 1246465, 1572864, 1572864};
static const int32_t kSympatheticDetuningsQ16[4] = {852, 721, 459, 1114};


inline Audio audio_add(Audio left, Audio right, NumericStats& stats) {
  return narrow_i32(add_wide(left, right, stats), stats);
}

inline Audio audio_sub(Audio left, Audio right, NumericStats& stats) {
  return narrow_i32(sub_wide(left, right, stats), stats);
}

inline uint64_t magnitude_i64(int64_t value) {
  return value < 0
      ? static_cast<uint64_t>(-(value + 1)) + UINT64_C(1)
      : static_cast<uint64_t>(value);
}

inline uint32_t magnitude_i32(int32_t value) {
  return value < 0
      ? static_cast<uint32_t>(-(static_cast<int64_t>(value)))
      : static_cast<uint32_t>(value);
}

inline Unit clamp_unit(Unit value, NumericStats& stats) {
  if (value > kUnitOne) {
    note_invalid_range(stats);
    return kUnitOne;
  }
  return value;
}

inline Unit unit_complement(Unit value, NumericStats& stats) {
  value = clamp_unit(value, stats);
  return kUnitOne - value;
}

inline Unit unit_to_unit_bend(Unit value, NumericStats& stats) {
  value = clamp_unit(value, stats);
  const Unit complement = kUnitOne - value;
  return kUnitOne - multiply_unit(complement, complement, stats);
}

inline Gain unit_to_gain(Unit value, NumericStats& stats) {
  return narrow_i32(round_shift(static_cast<int64_t>(value), 2u, stats), stats);
}

inline Unit ratio_q30_to_unit(uint64_t ratio_q30, NumericStats& stats) {
  if (ratio_q30 > (UINT64_C(1) << 30u)) {
    note_invalid_range(stats);
    return kUnitOne;
  }
  return static_cast<Unit>(ratio_q30 << 1u);
}

inline int32_t scale_q31_signed(int32_t value, Unit scale,
                                NumericStats& stats) {
  return mul_q(value, unit_to_gain(scale, stats), 29u, stats);
}


inline Delay delay_scale_unit(Delay value, Unit scale, NumericStats& stats) {
  if (scale > kUnitOne) {
    note_invalid_range(stats);
    scale = kUnitOne;
  }
  const bool negative = value < 0;
  const uint64_t magnitude = magnitude_i64(value);
  const uint64_t whole = (magnitude >> 31u) * scale;
  const uint64_t remainder_product =
      (magnitude & UINT64_C(0x7fffffff)) * scale;
  const uint64_t fractional = static_cast<uint64_t>(round_shift(
      static_cast<int64_t>(remainder_product), 31u, stats));
  const uint64_t result = whole + fractional;
  if (!negative) {
    if (result > static_cast<uint64_t>(INT64_MAX)) {
      note_saturation(stats);
      return INT64_MAX;
    }
    return static_cast<Delay>(result);
  }
  if (result == (UINT64_C(1) << 63u)) {
    return INT64_MIN;
  }
  return -static_cast<Delay>(result);
}

inline Delay delay_scale_gain(Delay value, Gain scale, NumericStats& stats) {
  const int64_t product = value * static_cast<int64_t>(scale);
  return round_shift(product, 29u, stats);
}

inline int32_t q24_from_frequency(Frequency frequency, NumericStats& stats) {
  if (frequency > static_cast<Frequency>(INT64_MAX)) {
    note_invalid_range(stats);
    return INT32_MAX;
  }
  return narrow_i32(round_shift(static_cast<int64_t>(frequency), 16u, stats),
                    stats);
}

inline Frequency frequency_from_q24(int32_t frequency_q24,
                                    NumericStats& stats) {
  if (frequency_q24 < 0) {
    note_invalid_range(stats);
    return 0u;
  }
  return static_cast<Frequency>(static_cast<uint32_t>(frequency_q24)) << 16u;
}

inline uint32_t control_coordinate_q8(Unit control, NumericStats& stats) {
  control = clamp_unit(control, stats);
  return control >> 15u;
}

inline int32_t semitones_from_q31(int64_t value_q31, int32_t span,
                                  NumericStats& stats) {
  const int64_t product = value_q31 * static_cast<int64_t>(span) * INT64_C(65536);
  return narrow_i32(round_shift(product, 31u, stats), stats);
}

inline int32_t semitones_from_unit(Unit value, Unit origin, int32_t span,
                                   NumericStats& stats) {
  return semitones_from_q31(static_cast<int64_t>(value) - origin, span,
                            stats);
}

inline uint32_t tangent_q24(uint32_t frequency_q24,
                            TangentApproximation approximation,
                            NumericStats& stats) {
  if (frequency_q24 > kQ24Nyquist499) {
    frequency_q24 = kQ24Nyquist499;
  }
  const int32_t f = static_cast<int32_t>(frequency_q24);
  const int32_t f2 = mul_q(f, f, 24u, stats);
  int32_t polynomial_q16 = kPiQ16;

  if (approximation == kTangentDirty) {
    const int32_t term = mul_q(f2, INT32_C(759165), 24u, stats);
    polynomial_q16 = narrow_i32(add_wide(kPiQ16, term, stats), stats);
  } else if (approximation == kTangentFast) {
    const int32_t f4 = mul_q(f2, f2, 24u, stats);
    const int32_t term_a = mul_q(f2, INT32_C(662441), 24u, stats);
    const int32_t term_b = mul_q(f4, INT32_C(3656082), 24u, stats);
    polynomial_q16 = narrow_i32(
        add_wide(add_wide(kPiQ16, term_a, stats), term_b, stats), stats);
  } else {
    int32_t value_q16 = INT32_C(183493163);
    value_q16 = narrow_i32(add_wide(INT32_C(5666376),
                                    mul_q(f2, value_q16, 24u, stats), stats),
                           stats);
    value_q16 = narrow_i32(add_wide(INT32_C(10564751),
                                    mul_q(f2, value_q16, 24u, stats), stats),
                           stats);
    value_q16 = narrow_i32(add_wide(INT32_C(2675225),
                                    mul_q(f2, value_q16, 24u, stats), stats),
                           stats);
    value_q16 = narrow_i32(add_wide(INT32_C(677339),
                                    mul_q(f2, value_q16, 24u, stats), stats),
                           stats);
    polynomial_q16 = narrow_i32(add_wide(kPiQ16,
                                         mul_q(f2, value_q16, 24u, stats),
                                         stats), stats);
  }

  const int32_t g_q24 = mul_q(f, polynomial_q16, 16u, stats);
  return g_q24 <= 0 ? 0u : static_cast<uint32_t>(g_q24);
}

inline void configure_svf(SvfState& filter, uint32_t cutoff_q24,
                          uint32_t r_q30, TangentApproximation approximation,
                          NumericStats& stats) {
  const uint32_t g_q24 = tangent_q24(cutoff_q24, approximation, stats);
  svf_coefficients(g_q24, r_q30, filter.h, filter.a2, filter.a3, stats);
}

inline void process_svf(SvfState& filter, Audio input, Audio* bandpass,
                        Audio* lowpass, NumericStats& stats,
                        int64_t kick_bp_q31 = 0,
                        int64_t kick_lp_q31 = 0) {
  const int64_t bp_q56 = add_wide(
      sub_wide(
          add_wide(static_cast<int64_t>(filter.s1) * filter.h,
                   static_cast<int64_t>(input) * filter.a2, stats),
          static_cast<int64_t>(filter.s2) * filter.a2, stats),
      kick_bp_q31 * (INT64_C(1) << 25u), stats);
  const Audio bp = narrow_i32(round_shift(bp_q56, 31u, stats), stats);

  const Unit one_minus_a3 = kUnitOne - filter.a3;
  const int64_t lp_q56 = add_wide(
      add_wide(
          add_wide(static_cast<int64_t>(filter.s2) * one_minus_a3,
                   static_cast<int64_t>(filter.s1) * filter.a2, stats),
          static_cast<int64_t>(input) * filter.a3, stats),
      kick_lp_q31 * (INT64_C(1) << 25u), stats);
  const Audio lp = narrow_i32(round_shift(lp_q56, 31u, stats), stats);

  const int64_t new_s1_q30 = sub_wide(
      bp_q56,
      static_cast<int64_t>(filter.s1) * (INT64_C(1) << 30u), stats);
  const int64_t new_s2_q30 = sub_wide(
      lp_q56,
      static_cast<int64_t>(filter.s2) * (INT64_C(1) << 30u), stats);
  filter.s1 = narrow_i32(round_shift(new_s1_q30, 30u, stats), stats);
  filter.s2 = narrow_i32(round_shift(new_s2_q30, 30u, stats), stats);

  if (bandpass != 0) {
    *bandpass = bp;
  }
  if (lowpass != 0) {
    *lowpass = lp;
  }
}

inline Unit unit_from_audio(Audio value, NumericStats&) {
  if (value <= 0) {
    return 0u;
  }
  if (value >= kAudioOne) {
    return kUnitOne;
  }
  const int64_t scaled = static_cast<int64_t>(value) << 6u;
  return static_cast<Unit>(scaled);
}

inline Audio cosine_next(Audio& y0, Audio& y1, Gain coefficient,
                         NumericStats& stats) {
  const Audio temp = y0;
  y0 = audio_sub(scale_audio_gain(y0, coefficient, stats), y1, stats);
  y1 = temp;
  return audio_add(temp, kAudioHalf, stats);
}

inline Gain cosine_coefficient(Unit frequency, NumericStats& stats) {
  int64_t f_q29 = static_cast<int64_t>(frequency >> 2u) -
                  (static_cast<int64_t>(kGainOne) >> 2u);
  int64_t sign = 16;
  if (f_q29 < 0) {
    f_q29 = -f_q29;
  } else if (f_q29 > (static_cast<int64_t>(kGainOne) >> 1u)) {
    f_q29 -= static_cast<int64_t>(kGainOne) >> 1u;
  } else {
    sign = -16;
  }
  const int32_t f = narrow_i32(f_q29, stats);
  const int32_t one_minus_two_f = narrow_i32(
      sub_wide(kGainOne, static_cast<int64_t>(f) * 2, stats), stats);
  const int32_t product = mul_q(f, one_minus_two_f, 29u, stats);
  return narrow_i32(static_cast<int64_t>(product) * sign, stats);
}

inline void initialize_lfo(LfoState& lfo, uint32_t frequency_q29,
                           NumericStats& stats) {
  int64_t f_q29 = static_cast<int64_t>(frequency_q29) -
                  static_cast<int64_t>(kGainOne) / 4;
  int64_t sign = 16;
  if (f_q29 < 0) {
    f_q29 = -f_q29;
  } else if (f_q29 > static_cast<int64_t>(kGainOne) / 2) {
    f_q29 -= static_cast<int64_t>(kGainOne) / 2;
  } else {
    sign = -16;
  }
  const int32_t f = narrow_i32(f_q29, stats);
  const int32_t one_minus_two_f = narrow_i32(
      sub_wide(kGainOne, static_cast<int64_t>(f) * 2, stats), stats);
  const int32_t product = mul_q(f, one_minus_two_f, 29u, stats);
  lfo.coefficient = narrow_i32(static_cast<int64_t>(product) * sign, stats);
  lfo.initial_amplitude = narrow_i32(
      round_shift(static_cast<int64_t>(lfo.coefficient), 6u, stats), stats);
  lfo.y0 = kAudioHalf;
  lfo.y1 = lfo.initial_amplitude;
}

inline Audio next_lfo(LfoState& lfo, NumericStats& stats) {
  return cosine_next(lfo.y0, lfo.y1, lfo.coefficient, stats);
}

inline Audio crossfade_audio(Audio a, Audio b, Unit phase,
                             NumericStats& stats) {
  const Audio difference = narrow_i32(sub_wide(b, a, stats), stats);
  return audio_add(a, scale_audio_unit(difference, phase, stats), stats);
}

inline Unit square_complement(Unit value, NumericStats& stats) {
  const Unit complement = unit_complement(value, stats);
  return multiply_unit(complement, complement, stats);
}

inline Frequency excitation_cutoff(Frequency frequency, Unit brightness,
                                   NumericStats& stats) {
  const Unit brightness_curve = unit_to_unit_bend(brightness, stats);
  const int32_t semitones = semitones_from_unit(
      brightness_curve, kUnitHalf, 96, stats);
  const uint64_t ratio_q30 = semitones_to_ratio_q30(semitones, stats);
  Frequency cutoff = scale_frequency_q30(frequency, ratio_q30, stats);
  if (cutoff > kFrequencyNyquist499) {
    cutoff = kFrequencyNyquist499;
  }
  return cutoff;
}

inline void modal_strike_kick(Frequency cutoff, const SvfState& filter,
                              int64_t& kick_bp_q31, int64_t& kick_lp_q31,
                              NumericStats& stats) {
  int32_t cutoff_q24 = q24_from_frequency(cutoff, stats);
  if (cutoff_q24 < 0) {
    cutoff_q24 = 0;
  }
  if (static_cast<uint32_t>(cutoff_q24) > kQ24Nyquist499) {
    cutoff_q24 = static_cast<int32_t>(kQ24Nyquist499);
  }
  const int32_t square_q24 = mul_q(cutoff_q24, cutoff_q24, 24u, stats);
  const int32_t semitones_q16 = narrow_i32(
      round_shift(static_cast<int64_t>(square_q24) * 24, 8u, stats), stats);
  const uint64_t ratio_q30 = semitones_to_ratio_q30(semitones_q16, stats);
  const Unit strike_gain = static_cast<Unit>(
      round_shift(static_cast<int64_t>(ratio_q30), 1u, stats));
  const uint32_t g_q24 = tangent_q24(
      static_cast<uint32_t>(cutoff_q24), kTangentDirty, stats);
  const Unit filtered_gain = multiply_unit(strike_gain, filter.h, stats);

  Unit bandpass_q31;
  if (cutoff_q24 == 0) {
    bandpass_q31 = static_cast<Unit>(mul_q(
        static_cast<int32_t>(filtered_gain), kPiQ16, 16u, stats));
  } else {
    const uint64_t numerator =
        static_cast<uint64_t>(filtered_gain) * g_q24;
    const uint64_t quotient = divide_u64_round(
        numerator, static_cast<uint32_t>(cutoff_q24), stats);
    if (quotient > kUnitOne) {
      note_saturation(stats);
      bandpass_q31 = kUnitOne;
    } else {
      bandpass_q31 = static_cast<Unit>(quotient);
    }
  }

  const int64_t lowpass_q31 = round_shift(
      static_cast<int64_t>(bandpass_q31) * g_q24, 24u, stats);
  if (lowpass_q31 < 0) {
    note_invalid_range(stats);
    kick_lp_q31 = 0;
  } else if (static_cast<uint64_t>(lowpass_q31) > kUnitOne) {
    note_saturation(stats);
    kick_lp_q31 = kUnitOne;
  } else {
    kick_lp_q31 = lowpass_q31;
  }
  kick_bp_q31 = static_cast<int64_t>(bandpass_q31);

}

inline int32_t clamp_frequency_q24(Frequency frequency, NumericStats& stats) {
  int32_t result = q24_from_frequency(frequency, stats);
  if (result < 0) {
    return 0;
  }
  if (static_cast<uint32_t>(result) > kQ24Nyquist499) {
    result = static_cast<int32_t>(kQ24Nyquist499);
  }
  return result;
}

inline int32_t interpolate_unit_step(Unit current, Unit target) {
  const int64_t difference = static_cast<int64_t>(target) - current;
  return static_cast<int32_t>(difference /
                              static_cast<int64_t>(kCoreTickSamples));
}

inline Unit advance_unit(Unit value, int32_t step, NumericStats& stats) {
  const int64_t advanced = static_cast<int64_t>(value) + step;
  if (advanced < 0) {
    note_invalid_range(stats);
    return 0u;
  }
  if (advanced > static_cast<int64_t>(kUnitOne)) {
    note_invalid_range(stats);
    return kUnitOne;
  }
  return static_cast<Unit>(advanced);
}

inline int32_t interpolate_signed_step(int32_t current, int32_t target) {
  const int64_t difference = static_cast<int64_t>(target) - current;
  return static_cast<int32_t>(difference / static_cast<int64_t>(kCoreTickSamples));
}

inline Delay interpolate_delay_step(Delay current, Delay target) {
  return (target - current) / static_cast<int64_t>(kCoreTickSamples);
}

inline Frequency glide_frequency(Frequency current, Frequency target,
                                Unit coefficient, NumericStats& stats) {
  const int32_t current_q24 = q24_from_frequency(current, stats);
  const int32_t target_q24 = q24_from_frequency(target, stats);
  const int32_t difference = narrow_i32(
      sub_wide(target_q24, current_q24, stats), stats);
  const int32_t scaled = mul_q(difference, unit_to_gain(coefficient, stats),
                               29u, stats);
  const int32_t result = narrow_i32(add_wide(current_q24, scaled, stats), stats);
  return frequency_from_q24(result, stats);
}

template <unsigned N>
inline Audio delay_read_linear(const Audio (&line)[N], unsigned write_index,
                               Delay delay, NumericStats& stats) {
  if (delay < 0) {
    note_invalid_range(stats);
    delay = 0;
  }
  const uint64_t raw = static_cast<uint64_t>(delay);
  const unsigned whole = static_cast<unsigned>(raw >> 20u);
  const uint32_t fractional = static_cast<uint32_t>(raw & UINT64_C(0xfffff));
  const Unit phase = static_cast<Unit>(fractional << 11u);
  const unsigned index = (write_index + whole) & (N - 1u);
  const Audio a = line[index];
  const Audio b = line[(index + 1u) & (N - 1u)];
  const Audio difference = narrow_i32(sub_wide(b, a, stats), stats);
  return audio_add(a, scale_audio_unit(difference, phase, stats), stats);
}

template <unsigned N>
inline Audio delay_read_hermite(const Audio (&line)[N], unsigned write_index,
                                Delay delay, NumericStats& stats) {
  const Delay negative_limit = -(INT64_C(1) << 20u);
  if (delay < negative_limit) {
    note_invalid_range(stats);
    delay = negative_limit;
  }
  const bool negative = delay < 0;
  const uint64_t raw = magnitude_i64(delay);
  const unsigned whole = static_cast<unsigned>(raw >> 20u);
  const uint32_t fractional = static_cast<uint32_t>(raw & UINT64_C(0xfffff));
  const unsigned index = negative
      ? (write_index - whole) & (N - 1u)
      : (write_index + whole) & (N - 1u);
  const Audio xm1 = line[(index - 1u) & (N - 1u)];
  const Audio x0 = line[index];
  const Audio x1 = line[(index + 1u) & (N - 1u)];
  const Audio x2 = line[(index + 2u) & (N - 1u)];

  const int64_t c_wide = round_shift(sub_wide(x1, xm1, stats), 1u, stats);
  const int32_t c = narrow_i32(c_wide, stats);
  const int32_t v = narrow_i32(sub_wide(x0, x1, stats), stats);
  const int32_t w = narrow_i32(add_wide(c, v, stats), stats);
  const int32_t x2_minus_x0 = narrow_i32(sub_wide(x2, x0, stats), stats);
  const int64_t a_wide = add_wide(
      add_wide(w, v, stats),
      round_shift(static_cast<int64_t>(x2_minus_x0), 1u, stats), stats);
  const int32_t a = narrow_i32(a_wide, stats);
  const int32_t b_neg = narrow_i32(add_wide(w, a, stats), stats);

  if (negative) {
    const Gain phase = -static_cast<Gain>(fractional << 9u);
    Audio value = audio_sub(scale_audio_gain(a, phase, stats), b_neg, stats);
    value = audio_add(scale_audio_gain(value, phase, stats), c, stats);
    return audio_add(scale_audio_gain(value, phase, stats), x0, stats);
  }
  const Unit phase = static_cast<Unit>(fractional << 11u);
  Audio value = audio_sub(scale_audio_unit(a, phase, stats), b_neg, stats);
  value = audio_add(scale_audio_unit(value, phase, stats), c, stats);
  return audio_add(scale_audio_unit(value, phase, stats), x0, stats);
}

template <unsigned N>
inline void delay_write(Audio (&line)[N], unsigned& write_index, Audio value) {
  line[write_index] = value;
  write_index = (write_index - 1u) & (N - 1u);
}

inline Audio allpass_process(StringBankState& string, Audio input,
                             unsigned delay_samples, NumericStats& stats) {
  const unsigned index = (string.stretch_index + delay_samples) &
                         (kStringStretchDelayLength - 1u);
  const Audio read = string.stretch_delay[index];
  const Audio write = audio_add(
      input, scale_audio_gain(read, string.dispersion_gain, stats), stats);
  delay_write(string.stretch_delay, string.stretch_index, write);
  return audio_sub(read,
                   scale_audio_gain(write, string.dispersion_gain, stats),
                   stats);
}

inline Audio soft_limit(Audio value, NumericStats& stats) {
  const uint64_t square_q50 =
      static_cast<uint64_t>(static_cast<int64_t>(value) * value);
  const uint64_t square_q30 = static_cast<uint64_t>(round_shift(
      static_cast<int64_t>(square_q50), 20u, stats));
  const uint64_t base = UINT64_C(27) << 30u;
  const uint64_t numerator = base + square_q30;
  const uint64_t denominator = base + UINT64_C(9) * square_q30;
  const Unit ratio = ratio_q31(numerator, denominator, stats);
  return scale_audio_unit(value, ratio, stats);
}

inline Unit symmetric_interval_factor(Unit structure, unsigned interval_index,
                                      NumericStats& stats) {
  if (interval_index == 0u) {
    return 0u;
  }
  Unit parameter = structure;
  for (unsigned i = 1u; i < interval_index; ++i) {
    const Unit remaining = kUnitOne - parameter;
    parameter = static_cast<Unit>(parameter +
        multiply_unit(remaining, kUnitTwentyPercent, stats));
  }
  return parameter;
}

inline int32_t sympathetic_interval_q16(Unit structure, unsigned index,
                                        NumericStats& stats) {
  if (index >= 5u) {
    const unsigned base_index = index - 5u;
    const int32_t base = sympathetic_interval_q16(structure, base_index, stats);
    return narrow_i32(add_wide(base, kSympatheticDetuningsQ16[base_index], stats),
                      stats);
  }
  if (index == 0u) {
    return 0;
  }

  const Unit parameter = symmetric_interval_factor(structure, index, stats);
  const int64_t note_q16 = round_shift(
      static_cast<int64_t>(parameter) * 7 * INT64_C(65536), 31u, stats);
  const unsigned integral = static_cast<unsigned>(note_q16 >> 16u);
  const Unit fraction = static_cast<Unit>(
      (static_cast<uint64_t>(note_q16) & UINT64_C(0xffff)) << 15u);
  const Unit warped = [&stats, fraction]() -> Unit {
    if (fraction < kUnitHalf) {
      const Unit doubled = fraction << 1u;
      const Unit square = multiply_unit(doubled, doubled, stats);
      const Unit fourth = multiply_unit(square, square, stats);
      const Unit eighth = multiply_unit(fourth, fourth, stats);
      const Unit sixteenth = multiply_unit(eighth, eighth, stats);
      return multiply_unit(sixteenth, kUnitHalf, stats);
    }
    const Unit doubled_complement = (kUnitOne - fraction) << 1u;
    const Unit square = multiply_unit(doubled_complement,
                                     doubled_complement, stats);
    const Unit fourth = multiply_unit(square, square, stats);
    const Unit eighth = multiply_unit(fourth, fourth, stats);
    const Unit sixteenth = multiply_unit(eighth, eighth, stats);
    return kUnitOne - multiply_unit(sixteenth, kUnitHalf, stats);
  }();

  const int32_t a = kSympatheticNotesQ16[integral];
  const int32_t b = kSympatheticNotesQ16[integral + 1u];
  const int32_t delta = narrow_i32(sub_wide(b, a, stats), stats);
  const int32_t scaled = scale_q31_signed(delta, warped, stats);
  return narrow_i32(add_wide(a, scaled, stats), stats);
}

inline Frequency sympathetic_frequency(Frequency root, Unit structure,
                                      unsigned index, NumericStats& stats) {
  const int32_t interval = sympathetic_interval_q16(structure, index, stats);
  return scale_frequency_q30(root,
      semitones_to_ratio_q30(interval, stats), stats);
}

inline int32_t dispersion_from_structure(Unit structure,
                                         NumericStats& stats) {
  if (structure < UINT32_C(515396076)) {
    const int32_t difference = narrow_i32(
        static_cast<int64_t>(structure) - INT64_C(515396076), stats);
    return mul_q(difference, INT32_C(69893882), 24u, stats);
  }
  if (structure > UINT32_C(558345748)) {
    const int32_t difference = narrow_i32(
        static_cast<int64_t>(structure) - INT64_C(558345748), stats);
    return mul_q(difference, INT32_C(22671891), 24u, stats);
  }
  return 0;
}

inline Gain dispersion_allpass_gain(int32_t dispersion,
                                    NumericStats& stats) {
  const uint32_t absolute = magnitude_i32(dispersion);
  const uint64_t numerator_q30 = static_cast<uint64_t>(absolute) >> 1u;
  const uint64_t denominator_q30 =
      (static_cast<uint64_t>(UINT32_C(161061274)) + numerator_q30);
  if (denominator_q30 == 0u) {
    note_invalid_division(stats);
    return 0;
  }
  const Unit ratio = ratio_q31(numerator_q30, denominator_q30, stats);
  const Unit scaled = multiply_unit(ratio, kUnitSixtyOneEightPercent, stats);
  const Gain magnitude = unit_to_gain(scaled, stats);
  return dispersion > 0 ? -magnitude : magnitude;
}

inline Unit noise_denominator_reciprocal(Unit noise_filter,
                                         unsigned* left_shift,
                                         NumericStats& stats) {
  const uint64_t denominator_q29 =
      static_cast<uint64_t>(UINT32_C(107374182)) +
      static_cast<uint64_t>(noise_filter >> 2u);
  unsigned shift = 0u;
  uint64_t scaled_denominator = denominator_q29;
  while (scaled_denominator < static_cast<uint64_t>(kGainOne) && shift < 4u) {
    scaled_denominator <<= 1u;
    ++shift;
  }
  if (scaled_denominator < static_cast<uint64_t>(kGainOne)) {
    note_invalid_range(stats);
    *left_shift = 0u;
    return 0u;
  }
  *left_shift = shift;
  return ratio_q31(static_cast<uint64_t>(kGainOne), scaled_denominator, stats);
}

inline Unit damping_law(Unit damping, NumericStats& stats) {
  return unit_to_unit_bend(damping, stats);
}

inline int32_t damping_cutoff_q16(Unit damping, Unit brightness,
                                  NumericStats& stats) {
  const Unit damping_square = multiply_unit(damping, damping, stats);
  const Unit brightness_square = multiply_unit(brightness, brightness, stats);
  const int64_t damping_part = round_shift(
      static_cast<int64_t>(damping_square) * 48 * INT64_C(65536), 31u, stats);
  const int64_t brightness_part = round_shift(
      static_cast<int64_t>(brightness_square) * 24 * INT64_C(65536), 31u,
      stats);
  return narrow_i32(add_wide(add_wide(INT64_C(24) << 16u, damping_part, stats),
                             brightness_part, stats), stats);
}

struct StringFilterTargets {
  Unit fir_damping;
  Unit fir_brightness;
  Unit damping_compensation;
};

inline StringFilterTargets calculate_string_filter(StringBankState& string,
                                                   Unit damping,
                                                   Unit brightness,
                                                   NumericStats& stats) {
  const Unit lf_damping = damping_law(damping, stats);
  const int32_t rt60_semitones = semitones_from_q31(lf_damping, 96, stats);
  const uint64_t rt60_ratio_q30 =
      semitones_to_ratio_q30(rt60_semitones, stats);
  const uint64_t rt60_q16 = static_cast<uint64_t>(round_shift(
      static_cast<int64_t>(rt60_ratio_q30 * UINT64_C(3360)), 14u, stats));
  const uint64_t frequency_q20 = static_cast<uint64_t>(round_shift(
      static_cast<int64_t>(string.frequency), 20u, stats));
  const uint64_t denominator_q16 = static_cast<uint64_t>(round_shift(
      static_cast<int64_t>(frequency_q20 * rt60_q16), 20u, stats));

  uint64_t magnitude;
  if (denominator_q16 == 0u && string.frequency != 0u) {
    const uint64_t denominator_q40 =
        (string.frequency * rt60_q16) >> 16u;
    magnitude = denominator_q40 == 0u
        ? UINT64_C(127) << 16u
        : divide_u64_round(UINT64_C(120) << 56u, denominator_q40, stats);
  } else if (denominator_q16 == 0u) {
    note_invalid_division(stats);
    magnitude = UINT64_C(127) << 16u;
  } else {
    magnitude = divide_u64_round(
        UINT64_C(120) << 32u, denominator_q16, stats);
  }
  const uint64_t max_magnitude = UINT64_C(127) << 16u;
  if (magnitude > max_magnitude) {
    magnitude = max_magnitude;
  }
  const int32_t decay_semitones_q16 = -static_cast<int32_t>(magnitude);
  Unit damping_coefficient = ratio_q30_to_unit(
      semitones_to_ratio_q30(decay_semitones_q16, stats), stats);

  Unit fir_brightness = multiply_unit(brightness, brightness, stats);
  int32_t cutoff_q16 = damping_cutoff_q16(damping, brightness, stats);
  if (cutoff_q16 > (INT32_C(84) << 16u)) {
    cutoff_q16 = INT32_C(84) << 16u;
  }
  const uint64_t cutoff_ratio_q30 = semitones_to_ratio_q30(cutoff_q16, stats);
  Frequency damping_frequency =
      scale_frequency_q30(string.frequency, cutoff_ratio_q30, stats);
  if (damping_frequency > kFrequencyNyquist499) {
    damping_frequency = kFrequencyNyquist499;
  }

  if (damping >= UINT32_C(2040109466)) {
    const Unit to_infinite = static_cast<Unit>(
        (static_cast<uint64_t>(damping) - UINT32_C(2040109466)) * 20u);
    damping_coefficient = static_cast<Unit>(damping_coefficient +
        multiply_unit(to_infinite, kUnitOne - damping_coefficient, stats));
    fir_brightness = static_cast<Unit>(fir_brightness +
        multiply_unit(to_infinite, kUnitOne - fir_brightness, stats));

    const uint32_t target_q24 =
        (UINT32_C(4999) * kQ24One) / UINT32_C(10000);
    const int32_t current_q24 = q24_from_frequency(damping_frequency, stats);
    const int32_t difference_q24 = narrow_i32(
        sub_wide(target_q24, current_q24, stats), stats);
    const int32_t moved_q24 = mul_q(
        difference_q24, unit_to_gain(to_infinite, stats), 29u, stats);
    damping_frequency = frequency_from_q24(narrow_i32(
        add_wide(current_q24, moved_q24, stats), stats), stats);

    const int64_t cutoff_difference = (INT64_C(128) << 16u) - cutoff_q16;
    cutoff_q16 = narrow_i32(add_wide(cutoff_q16,
        round_shift(cutoff_difference * to_infinite, 31u, stats), stats), stats);
  }

  configure_svf(string.iir_filter,
      static_cast<uint32_t>(clamp_frequency_q24(damping_frequency, stats)),
      UINT32_C(0x80000000), kTangentAccurate, stats);

  const uint32_t shift_index_q8 = static_cast<uint32_t>(cutoff_q16) >> 8u;
  const Unit shift = lookup_u32_q8(kSvfShiftQ31, shift_index_q8, stats);
  StringFilterTargets targets;
  targets.fir_damping = damping_coefficient;
  targets.fir_brightness = fir_brightness;
  targets.damping_compensation = kUnitOne - shift;
  return targets;
}

inline Audio divide_noise(Audio noise, Unit noise_filter,
                          NumericStats& stats) {
  unsigned left_shift = 0u;
  const Unit reciprocal = noise_denominator_reciprocal(
      noise_filter, &left_shift, stats);
  Audio value = scale_audio_unit(noise, reciprocal, stats);
  if (left_shift != 0u) {
    const int64_t scaled = static_cast<int64_t>(value) *
                           (INT64_C(1) << left_shift);
    value = narrow_i32(scaled, stats);
  }
  return value;
}

inline Unit clamped_string_position(Unit position, NumericStats& stats) {
  const uint32_t distance = position >= kUnitHalf
      ? position - kUnitHalf
      : kUnitHalf - position;
  const Unit shrink = multiply_unit(distance, kUnitNinetyEightPercent, stats);
  return kUnitHalf - shrink;
}

inline Unit position_modulated(Unit position, Unit lfo, NumericStats& stats) {
  const uint32_t distance = position >= kUnitHalf
      ? position - kUnitHalf
      : kUnitHalf - position;
  const Unit amount = multiply_unit(kUnitHalf - distance,
                                    kUnitNinetyPercent, stats);
  const Unit modulation = multiply_unit(lfo, amount, stats);
  const uint64_t result = static_cast<uint64_t>(position) + modulation;
  return result > kUnitOne ? kUnitOne : static_cast<Unit>(result);
}

inline Unit decay_noise_filter(Unit brightness, NumericStats& stats) {
  const int32_t semitones = semitones_from_q31(
      static_cast<int64_t>(brightness) - kUnitOne, 48, stats);
  return ratio_q30_to_unit(semitones_to_ratio_q30(semitones, stats), stats);
}

inline Audio process_fir_damping(StringBankState& string, Audio input,
                                 NumericStats& stats) {
  const Unit h0 = static_cast<Unit>(kUnitHalf + (string.fir_brightness >> 1u));
  const Unit h1 = static_cast<Unit>((kUnitOne - string.fir_brightness) >> 2u);
  const Audio x_plus_x2 = audio_add(input, string.fir_x2, stats);
  const Audio term0 = scale_audio_unit(string.fir_x1, h0, stats);
  const Audio term1 = scale_audio_unit(x_plus_x2, h1, stats);
  const Audio weighted = audio_add(term0, term1, stats);
  const Audio output = scale_audio_unit(weighted, string.fir_damping, stats);
  string.fir_x2 = string.fir_x1;
  string.fir_x1 = input;
  string.fir_damping = advance_unit(
      string.fir_damping, string.fir_damping_step, stats);
  string.fir_brightness = advance_unit(
      string.fir_brightness, string.fir_brightness_step, stats);
  return output;
}


}  // namespace

void Core::initialize_zeroed() {
  rng_state_ = UINT32_C(0x21);
  active_model_ = UINT32_MAX;
  strike_pending_ = 0u;
  limiter_.envelope = kAudioHalf;
  exciter_.dc_coefficient = kDcPole;
}

void Core::reset_modal_history() {
  memset(&modal_, 0, sizeof(modal_));
}

void Core::reset_string_history(unsigned index) {
  if (index >= kStringBankVoices) {
    note_invalid_index(stats_);
    return;
  }
  memset(&strings_[index], 0, sizeof(strings_[index]));
  memset(&pluckers_[index], 0, sizeof(pluckers_[index]));
}

void Core::configure_modal(Frequency frequency, const Patch& patch) {
  const int32_t base_q24 = q24_from_frequency(frequency, stats_);
  const int32_t stiffness_q29 = lookup_i32_q8(
      kStiffnessQ29, control_coordinate_q8(patch.structure, stats_), stats_);
  int32_t stiffness_q24 = narrow_i32(
      round_shift(stiffness_q29, 5u, stats_), stats_);
  const uint32_t four_decades_q16 = lookup_u32_q8(
      kFourDecadesQ16, control_coordinate_q8(patch.damping, stats_), stats_);
  const uint32_t q_q8 = static_cast<uint32_t>(round_shift(
      static_cast<int64_t>(four_decades_q16) * kFourDecadesScale, 8u, stats_));

  const Unit structure_attenuation = square_complement(patch.structure, stats_);
  const Unit attenuation_squared = multiply_unit(structure_attenuation,
                                                  structure_attenuation, stats_);
  const Unit attenuation_fourth = multiply_unit(attenuation_squared,
                                                 attenuation_squared, stats_);
  const Unit attenuation_eighth = multiply_unit(attenuation_fourth,
                                                 attenuation_fourth, stats_);
  const Unit brightness = multiply_unit(patch.brightness, patch.brightness, stats_);
  const Unit brightness_reduction = multiply_unit(attenuation_eighth,
                                                   UINT32_C(429496730), stats_);
  const Unit effective_brightness = multiply_unit(
      brightness, kUnitOne - brightness_reduction, stats_);
  const Unit brightness_bend = unit_to_unit_bend(effective_brightness, stats_);
  Unit q_loss = static_cast<Unit>(kUnitFifteenPercent +
      multiply_unit(brightness_bend, kUnitEightyFivePercent, stats_));
  const Unit damping_rate = multiply_unit(
      unit_to_unit_bend(patch.structure, stats_), kUnitTenPercent, stats_);
  uint32_t current_q_q8 = q_q8;

  int32_t stretch_factor_q24 = static_cast<int32_t>(kQ24One);
  int32_t harmonic_q24 = base_q24;
  unsigned mode_count = 0u;
  for (unsigned i = 0u; i < kModalBankModes; ++i) {
    int32_t partial_q24 = mul_q(harmonic_q24, stretch_factor_q24, 24u, stats_);
    if (partial_q24 < 0) {
      partial_q24 = 0;
    }
    if (static_cast<uint32_t>(partial_q24) >=
        (UINT32_C(49) * kQ24One) / UINT32_C(100)) {
      partial_q24 = static_cast<int32_t>(
          (UINT32_C(49) * kQ24One) / UINT32_C(100));
    } else {
      mode_count = i + 1u;
    }

    ModalModeState& mode = modal_.modes[i];
    mode.frequency = frequency_from_q24(partial_q24, stats_);
    mode.q_q8 = current_q_q8;
    const uint64_t fq_q8 = static_cast<uint64_t>(
        static_cast<uint32_t>(partial_q24)) * current_q_q8;
    const uint64_t resonance_q30 = (UINT64_C(1) << 30u) +
        static_cast<uint64_t>(round_shift(static_cast<int64_t>(fq_q8), 2u, stats_));
    const Unit reciprocal_q31 = ratio_q31(UINT64_C(1) << 30u,
                                           resonance_q30, stats_);
    const uint32_t r_q30 = static_cast<uint32_t>(round_shift(
        static_cast<int64_t>(reciprocal_q31), 1u, stats_));
    configure_svf(mode.filter, static_cast<uint32_t>(partial_q24), r_q30,
                  kTangentFast, stats_);

    const int64_t stretch_next = add_wide(stretch_factor_q24,
                                          stiffness_q24, stats_);
    stretch_factor_q24 = narrow_i32(stretch_next, stats_);
    if (stiffness_q24 < 0) {
      stiffness_q24 = mul_q(stiffness_q24, INT32_C(499289948), 29u, stats_);
    } else {
      stiffness_q24 = mul_q(stiffness_q24, INT32_C(526133494), 29u, stats_);
    }

    const Unit loss_gap = kUnitOne - q_loss;
    q_loss = static_cast<Unit>(q_loss +
        multiply_unit(damping_rate, loss_gap, stats_));
    const Gain q_loss_q29 = unit_to_gain(q_loss, stats_);
    const int32_t updated_q_q8 = mul_q(static_cast<int32_t>(current_q_q8),
                                       q_loss_q29, 29u, stats_);
    current_q_q8 = updated_q_q8 < 0
        ? 0u : static_cast<uint32_t>(updated_q_q8);
    harmonic_q24 = narrow_i32(add_wide(harmonic_q24, base_q24, stats_), stats_);
  }


  modal_.base_frequency = frequency;
  modal_.structure = patch.structure;
  modal_.brightness = patch.brightness;
  modal_.damping = patch.damping;
  modal_.position = patch.position;
  modal_.mode_count = mode_count;
}

void Core::render_modal(Frequency frequency, const Patch& patch, bool strike,
                        Audio out[kCoreTickSamples],
                        Audio aux[kCoreTickSamples]) {
  configure_modal(frequency, patch);
  for (unsigned i = 0u; i < kCoreTickSamples; ++i) {
    out[i] = 0;
    aux[i] = 0;
    input_[i] = 0;
  }

  const Frequency cutoff = excitation_cutoff(frequency, patch.brightness, stats_);
  configure_svf(exciter_.filter, clamp_frequency_q24(cutoff, stats_),
                UINT32_C(715827883), kTangentDirty, stats_);
  int64_t kick_bp_q31 = 0;
  int64_t kick_lp_q31 = 0;
  if (strike) {
    modal_strike_kick(cutoff, exciter_.filter,
                      kick_bp_q31, kick_lp_q31, stats_);
  }

  const int32_t position_step = interpolate_unit_step(
      modal_.previous_position, patch.position);
  Unit position_unit = modal_.previous_position;

  for (unsigned sample = 0u; sample < kCoreTickSamples; ++sample) {
    position_unit = advance_unit(position_unit, position_step, stats_);

    Audio excitation = 0;
    process_svf(exciter_.filter, input_[sample], 0, &excitation, stats_,
                sample == 0u && strike ? kick_bp_q31 : 0,
                sample == 0u && strike ? kick_lp_q31 : 0);

    excitation = scale_audio_unit(excitation, UINT32_C(0x10000000), stats_);
    const Gain coefficient = cosine_coefficient(position_unit, stats_);
    Audio oscillator_y0 = kAudioHalf;
    Audio oscillator_y1 = narrow_i32(
        round_shift(static_cast<int64_t>(coefficient), 6u, stats_), stats_);

    unsigned mode_index = 0u;
    while (mode_index < modal_.mode_count && mode_index < kModalBankModes) {
      Audio bandpass = 0;
      process_svf(modal_.modes[mode_index].filter, excitation,
                  &bandpass, 0, stats_);
      const Unit odd_weight = unit_from_audio(
          cosine_next(oscillator_y0, oscillator_y1, coefficient, stats_), stats_);
      out[sample] = audio_add(out[sample],
          scale_audio_unit(bandpass, odd_weight, stats_), stats_);
      ++mode_index;

      if (mode_index < kModalBankModes) {
        process_svf(modal_.modes[mode_index].filter, excitation,
                    &bandpass, 0, stats_);
        const Unit even_weight = unit_from_audio(
            cosine_next(oscillator_y0, oscillator_y1, coefficient, stats_), stats_);
        aux[sample] = audio_add(aux[sample],
            scale_audio_unit(bandpass, even_weight, stats_), stats_);
        ++mode_index;
      }
    }
  }
  modal_.previous_position = patch.position;
}

void Core::render_sympathetic(Frequency frequency, const Patch& patch,
                              bool strike, Audio out[kCoreTickSamples],
                              Audio aux[kCoreTickSamples]) {
  render_string_bank(frequency, patch, strike, false, true, out, aux);
}

void Core::render_dispersive(Frequency frequency, const Patch& patch,
                             bool strike, Audio out[kCoreTickSamples],
                             Audio aux[kCoreTickSamples]) {
  render_string_bank(frequency, patch, strike, true, false, out, aux);
}

void Core::render_string_bank(Frequency frequency, const Patch& patch,
                              bool strike, bool dispersion_enabled,
                              bool sympathetic,
                              Audio out[kCoreTickSamples],
                              Audio aux[kCoreTickSamples]) {
  const unsigned string_count = sympathetic ? kStringBankVoices : 1u;
  for (unsigned sample = 0u; sample < kCoreTickSamples; ++sample) {
    out[sample] = 0;
    aux[sample] = 0;
    input_[sample] = 0;
    sympathetic_input_[sample] = 0;
  }

  const Frequency cutoff = excitation_cutoff(frequency, patch.brightness, stats_);
  if (strike) {
    PluckerState& plucker = pluckers_[0];
    Delay comb_period = frequency_reciprocal_q20(frequency, stats_);
    const Unit pluck_position = static_cast<Unit>(
        multiply_unit(patch.position, UINT32_C(1932735283), stats_) +
        UINT32_C(107374182));
    comb_period = delay_scale_unit(comb_period, pluck_position, stats_);
    plucker.remaining_samples = static_cast<unsigned>(
        static_cast<uint64_t>(comb_period) >> 20u);
    plucker.comb_period = comb_period;
    while (plucker.comb_period >= (INT64_C(255) << 20u)) {
      plucker.comb_period = round_shift(plucker.comb_period, 1u, stats_);
    }
    plucker.comb_gain = mul_q(
        unit_to_gain(unit_complement(patch.position, stats_), stats_),
        kGainEightyPercent, 29u, stats_);

    uint32_t pluck_cutoff_q24 = static_cast<uint32_t>(
        clamp_frequency_q24(cutoff, stats_));
    if (pluck_cutoff_q24 > kQ24Nyquist499 / 8u) {
      pluck_cutoff_q24 = kQ24Nyquist499;
    } else {
      pluck_cutoff_q24 *= 8u;
    }
    configure_svf(plucker.filter, pluck_cutoff_q24,
                  UINT32_C(0x40000000), kTangentDirty, stats_);
  }

  for (unsigned sample = 0u; sample < kCoreTickSamples; ++sample) {
    PluckerState& plucker = pluckers_[0];
    Audio noise = 0;
    if (plucker.remaining_samples != 0u) {
      noise = noise_q25(lcg_next(rng_state_), stats_);
      --plucker.remaining_samples;
    }
    const Audio comb = delay_read_linear(plucker.delay, plucker.delay_index,
                                         plucker.comb_period, stats_);
    const Audio comb_feedback = scale_audio_gain(comb, plucker.comb_gain, stats_);
    const Audio pluck_sample = audio_add(noise, comb_feedback, stats_);
    delay_write(plucker.delay, plucker.delay_index, pluck_sample);
    Audio pluck_low = 0;
    process_svf(plucker.filter, pluck_sample, 0, &pluck_low, stats_);
    input_[sample] = pluck_low;

    const int64_t dc_feedback_raw =
        static_cast<int64_t>(exciter_.dc_state) * exciter_.dc_coefficient +
        exciter_.dc_feedback_remainder_q31;
    const Audio dc_feedback = narrow_i32(
        round_shift(dc_feedback_raw, 31u, stats_), stats_);
    const int64_t dc_feedback_rounded_raw =
        static_cast<int64_t>(dc_feedback) * (INT64_C(1) << 31u);
    exciter_.dc_feedback_remainder_q31 = narrow_i32(
        dc_feedback_raw - dc_feedback_rounded_raw, stats_);
    Audio dc_output = audio_add(dc_feedback, input_[sample], stats_);
    dc_output = audio_sub(dc_output, exciter_.previous_input, stats_);
    exciter_.previous_input = input_[sample];
    exciter_.dc_state = dc_output;
    input_[sample] = dc_output;
  }

  const int32_t target_dispersion = dispersion_enabled
      ? dispersion_from_structure(patch.structure, stats_)
      : 0;
  const Unit noise_filter = decay_noise_filter(patch.brightness, stats_);

  for (unsigned string_index = 0u; string_index < string_count; ++string_index) {
    StringBankState& string = strings_[string_index];
    if (string.initialized == 0u) {
      const Frequency initial_frequency =
          (UINT64_C(220) << 40u) / UINT64_C(48000);
      string.frequency = initial_frequency;
      string.previous_delay = frequency_reciprocal_q20(initial_frequency, stats_);
      string.delay_samples = string.previous_delay;
      initialize_lfo(string.lfo, kLfoFrequencyQ29[string_index], stats_);
      string.initialized = 1u;
    }
    const Audio lfo_value_audio = next_lfo(string.lfo, stats_);
    const Unit lfo_value = unit_from_audio(lfo_value_audio, stats_);

    Unit brightness = patch.brightness;
    Unit damping = patch.damping;
    Unit position = patch.position;
    Unit glide = kUnitOne;
    Frequency target_frequency = frequency;

    if (sympathetic && string_index != 0u) {
      brightness = unit_to_unit_bend(brightness, stats_);
      brightness = unit_to_unit_bend(brightness, stats_);
      damping = static_cast<Unit>(UINT32_C(1503238554) +
          multiply_unit(patch.damping, UINT32_C(579820585), stats_));
      position = position_modulated(patch.position, lfo_value, stats_);
      const int32_t glide_semitones = semitones_from_q31(
          static_cast<int64_t>(brightness) - kUnitOne, 36, stats_);
      glide = ratio_q30_to_unit(
          semitones_to_ratio_q30(glide_semitones, stats_), stats_);
      target_frequency = sympathetic_frequency(frequency, patch.structure,
                                                string_index, stats_);
    }

    if (string_index != 0u && sympathetic) {
      const Unit string_fraction = ratio_q31(string_index, string_count, stats_);
      const int64_t damping_delta =
          static_cast<int64_t>(kUnitNinetyFivePercent) - damping;
      damping = static_cast<Unit>(static_cast<int64_t>(damping) +
          round_shift(damping_delta * string_fraction, 31u, stats_));
    }


    string.frequency = glide_frequency(string.frequency, target_frequency,
                                       glide, stats_);
    Delay target_delay = frequency_reciprocal_q20(string.frequency, stats_);
    if (target_delay < kDelayMinimum) {
      target_delay = kDelayMinimum;
    } else if (target_delay > kDelayMaximum) {
      target_delay = kDelayMaximum;
    }

    const StringFilterTargets filter_targets = calculate_string_filter(
        string, damping, brightness, stats_);
    const Unit clamped_position = clamped_string_position(position, stats_);
    const int32_t dispersion = dispersion_enabled ? target_dispersion : 0;
    const Gain allpass_gain = dispersion_allpass_gain(dispersion, stats_);
    string.dispersion_gain = allpass_gain;

    string.brightness = brightness;
    string.damping = damping;
    string.position = clamped_position;
    string.dispersion = dispersion;
    string.fir_damping_step = interpolate_unit_step(
        string.fir_damping, filter_targets.fir_damping);
    string.fir_brightness_step = interpolate_unit_step(
        string.fir_brightness, filter_targets.fir_brightness);

    const Delay delay_step = interpolate_delay_step(string.previous_delay,
                                                    target_delay);
    const int32_t position_step = interpolate_unit_step(
        string.previous_position, clamped_position);
    const int32_t dispersion_step = interpolate_signed_step(
        string.previous_dispersion, dispersion);
    const int32_t damping_comp_step = interpolate_unit_step(
        string.previous_damping_compensation,
        filter_targets.damping_compensation);


    const uint64_t delay_word = static_cast<uint64_t>(target_delay);
    const uint64_t frequency_high = string.frequency >> 32u;
    const uint64_t high_product = delay_word * frequency_high;
    if (high_product >= static_cast<uint64_t>(kUnitOne) / 8u) {
      string.src_ratio = kUnitOne;
      string.src_phase = kUnitOne;
    } else {
      const uint64_t frequency_low =
          string.frequency & UINT64_C(0xffffffff);
      const uint64_t low_product = delay_word * frequency_low;
      uint64_t ratio_q31 = (high_product << 3u) + (low_product >> 29u);
      if ((low_product & ((UINT64_C(1) << 29u) - 1u)) >=
          (UINT64_C(1) << 28u)) {
        ++ratio_q31;
      }
      if (ratio_q31 >=
          static_cast<uint64_t>(kUnitOne - kUnitOne / 10000u)) {
        string.src_ratio = kUnitOne;
        string.src_phase = kUnitOne;
      } else {
        string.src_ratio = static_cast<Unit>(ratio_q31);
      }
    }

    Delay delay_value = string.previous_delay;
    Unit position_value = string.previous_position;
    int32_t dispersion_value = string.previous_dispersion;
    Unit damping_comp_value = string.previous_damping_compensation;

    for (unsigned sample = 0u; sample < kCoreTickSamples; ++sample) {
      const uint64_t phase_sum = static_cast<uint64_t>(string.src_phase) +
                                 string.src_ratio;
      if (phase_sum > kUnitOne) {
        string.src_phase = static_cast<Unit>(phase_sum - kUnitOne);
        delay_value = static_cast<Delay>(delay_value + delay_step);
        position_value = advance_unit(position_value, position_step, stats_);
        dispersion_value += dispersion_step;
        damping_comp_value = advance_unit(
            damping_comp_value, damping_comp_step, stats_);

        Delay delay = delay_value;
        const Delay comb_delay = delay_scale_unit(delay, position_value, stats_);
        delay = delay_scale_unit(delay, damping_comp_value, stats_);
        delay -= INT64_C(1) << 20u;

        Audio resonator_sample = 0;
        Unit noise_amount = 0u;
        Unit bridge_amount = 0u;
        Unit bridge_curving = 0u;
        Unit stretch_point = 0u;

        if (dispersion_enabled) {
          const Audio raw_noise = noise_q25(lcg_next(rng_state_), stats_);
          const Audio noise = divide_noise(raw_noise, noise_filter, stats_);
          const Audio noise_difference = audio_sub(noise,
              string.dispersion_noise, stats_);
          string.dispersion_noise = audio_add(string.dispersion_noise,
              scale_audio_unit(noise_difference, noise_filter, stats_), stats_);

          const uint32_t absolute_dispersion = magnitude_i32(dispersion_value);
          if (dispersion_value > static_cast<int32_t>(kUnitThreeQuarter)) {
            noise_amount = static_cast<Unit>(
                (static_cast<uint64_t>(dispersion_value) - kUnitThreeQuarter) * 4u);
          }
          noise_amount = multiply_unit(
              multiply_unit(noise_amount, noise_amount, stats_),
              kUnitTwoAndHalfPercent, stats_);
          if (dispersion_value < 0) {
            bridge_amount = absolute_dispersion;
          }
          bridge_curving = multiply_unit(
              multiply_unit(bridge_amount, bridge_amount, stats_),
              kUnitOnePercent, stats_);
          if (dispersion_value > 0) {
            stretch_point = multiply_unit(
                unit_to_unit_bend(static_cast<Unit>(dispersion_value), stats_),
                kUnitFortySevenFivePercent, stats_);
          }

          Gain delay_fm = kGainOne;
          const Audio noise_delta = scale_audio_unit(
              string.dispersion_noise, noise_amount, stats_);
          const Audio bridge_delta = scale_audio_unit(
              string.curved_bridge, bridge_curving, stats_);
          delay_fm = narrow_i32(add_wide(delay_fm,
              static_cast<int64_t>(noise_delta) * 16, stats_), stats_);
          delay_fm = narrow_i32(sub_wide(delay_fm,
              static_cast<int64_t>(bridge_delta) * 16, stats_), stats_);
          delay = delay_scale_gain(delay, delay_fm, stats_);

          const Delay ap_delay = delay_scale_unit(delay, stretch_point, stats_);
          const Delay main_delay = delay - ap_delay;
          if (ap_delay >= kDelayMinimum && main_delay >= kDelayMinimum) {
            resonator_sample = delay_read_hermite(
                string.delay, string.delay_index, main_delay, stats_);
            resonator_sample = allpass_process(
                string, resonator_sample,
                static_cast<unsigned>(static_cast<uint64_t>(ap_delay) >> 20u),
                stats_);
          } else {
            resonator_sample = delay_read_hermite(
                string.delay, string.delay_index, delay, stats_);
          }

          const Audio dc_feedback = scale_audio_gain(
              string.dc_state, unit_to_gain(kDcPole, stats_), stats_);
          const Audio dc_blocked = audio_sub(
              audio_add(dc_feedback, resonator_sample, stats_),
              string.dc_previous_input, stats_);
          string.dc_previous_input = resonator_sample;
          string.dc_state = dc_blocked;
          const Audio ac_difference = audio_sub(dc_blocked, resonator_sample, stats_);
          resonator_sample = audio_add(resonator_sample,
              scale_audio_unit(ac_difference, bridge_amount, stats_), stats_);

          const int64_t thresholded =
              static_cast<int64_t>(magnitude_i32(resonator_sample)) -
              INT64_C(838861);
          Audio bridge = 0;
          if (thresholded > 0) {
            const int64_t shaped = resonator_sample > 0
                ? thresholded * 2
                : -(thresholded * 3);
            bridge = narrow_i32(shaped, stats_);
          }
          string.curved_bridge = bridge;
        } else {
          resonator_sample = delay_read_hermite(
              string.delay, string.delay_index, delay, stats_);
        }

        const Audio source = (sympathetic && string_index != 0u)
            ? sympathetic_input_[sample] : input_[sample];
        resonator_sample = audio_add(resonator_sample, source, stats_);
        resonator_sample = process_fir_damping(string, resonator_sample, stats_);
        Audio filtered = 0;
        process_svf(string.iir_filter, resonator_sample, 0, &filtered, stats_);
        resonator_sample = filtered;
        delay_write(string.delay, string.delay_index, resonator_sample);

        string.out_previous = string.out_current;
        string.aux_previous = string.aux_current;
        string.out_current = resonator_sample;
        string.aux_current = delay_read_linear(
            string.delay, string.delay_index, comb_delay, stats_);
      } else {
        string.src_phase = static_cast<Unit>(phase_sum);
      }

      const Audio string_out = crossfade_audio(
          string.out_previous, string.out_current, string.src_phase, stats_);
      const Audio string_aux = crossfade_audio(
          string.aux_previous, string.aux_current, string.src_phase, stats_);
      string_out_[sample] = string_out;
      string_aux_[sample] = string_aux;
      out[sample] = audio_add(out[sample], string_out, stats_);
      aux[sample] = audio_add(aux[sample], string_aux, stats_);
      if (sympathetic && string_index == 0u) {
        const Audio difference = audio_sub(string_out, string_aux, stats_);
        sympathetic_input_[sample] = scale_audio_unit(
            difference, UINT32_C(53687091), stats_);
      }
    }

    string.previous_delay = delay_value;
    string.previous_position = position_value;
    string.previous_dispersion = dispersion_value;
    string.previous_damping_compensation = damping_comp_value;
    string.delay_samples = delay_value;
    if (sympathetic && string_index > 0u) {
      string.brightness = brightness;
    }
  }
}

void Core::render_limiter(Audio out[kCoreTickSamples],
                          Audio aux[kCoreTickSamples]) {
  const Gain pre_gain = active_model_ == 1u ? kGainOne : INT32_C(751619277);
  for (unsigned i = 0u; i < kCoreTickSamples; ++i) {
    const Audio left_pre = scale_audio_gain(out[i], pre_gain, stats_);
    const Audio right_pre = scale_audio_gain(aux[i], pre_gain, stats_);
    const int64_t difference = sub_wide(right_pre, left_pre, stats_);
    uint64_t peak = magnitude_i32(left_pre);
    const uint64_t right_peak = magnitude_i32(right_pre);
    const uint64_t difference_peak = magnitude_i64(difference);
    if (right_peak > peak) {
      peak = right_peak;
    }
    if (difference_peak > peak) {
      peak = difference_peak;
    }
    const Audio target = peak > static_cast<uint64_t>(INT32_MAX)
        ? (note_saturation(stats_), INT32_MAX)
        : static_cast<Audio>(peak);
    const int64_t error = static_cast<int64_t>(target) - limiter_.envelope;
    const uint32_t slope = error > 0 ? kQ31SlopeAttack : kQ31SlopeRelease;
    const Audio error_audio = narrow_i32(error, stats_);
    const Audio movement = scale_audio_unit(error_audio, slope, stats_);
    limiter_.envelope = narrow_i32(
        add_wide(limiter_.envelope, movement, stats_), stats_);

    Unit gain = kUnitOne;
    if (limiter_.envelope > kAudioOne) {
      gain = ratio_q31(static_cast<uint64_t>(kAudioOne),
                       static_cast<uint64_t>(limiter_.envelope), stats_);
    }
    const Audio limited_left = scale_audio_unit(
        scale_audio_unit(left_pre, gain, stats_), UINT32_C(1717986918), stats_);
    const Audio limited_right = scale_audio_unit(
        scale_audio_unit(right_pre, gain, stats_), UINT32_C(1717986918), stats_);
    out[i] = soft_limit(limited_left, stats_);
    aux[i] = soft_limit(limited_right, stats_);
  }
}

void Core::render_tick(Frequency frequency, const Patch& patch,
                       unsigned model, bool strike,
                       Audio out[kCoreTickSamples],
                       Audio aux[kCoreTickSamples]) {
  if (model > 2u) {
    note_invalid_range(stats_);
    for (unsigned i = 0u; i < kCoreTickSamples; ++i) {
      out[i] = 0;
      aux[i] = 0;
    }
    strike_pending_ = 0u;
    return;
  }

  Patch controls = patch;
  controls.structure = clamp_unit(controls.structure, stats_);
  controls.brightness = clamp_unit(controls.brightness, stats_);
  controls.damping = clamp_unit(controls.damping, stats_);
  controls.position = clamp_unit(controls.position, stats_);

  if (frequency == 0u) {
    strike_pending_ = 0u;
    for (unsigned i = 0u; i < kCoreTickSamples; ++i) {
      out[i] = 0;
      aux[i] = 0;
    }
    return;
  }

  if (active_model_ != model) {
    active_model_ = model;
    if (model == 0u) {
      reset_modal_history();
    } else {
      for (unsigned i = 0u; i < kStringBankVoices; ++i) {
        reset_string_history(i);
      }
    }
  }

  strike_pending_ = strike ? 1u : 0u;
  if (model == 0u) {
    render_modal(frequency, controls, strike_pending_ != 0u, out, aux);
  } else if (model == 1u) {
    render_sympathetic(frequency, controls, strike_pending_ != 0u, out, aux);
  } else {
    render_dispersive(frequency, controls, strike_pending_ != 0u, out, aux);
  }
  render_limiter(out, aux);
  strike_pending_ = 0u;
}

}  // namespace dsp
}  // namespace rings
