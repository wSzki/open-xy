#pragma once

#include <stdint.h>

#include "dsp.hpp"

namespace rings {
namespace dsp {

// Generated, read-only source tables. Every array has exactly one definition
// in tables.cpp. The Q format is part of each symbol's name or declaration.
extern const Audio kSineQ25[5121];
extern const uint32_t kFourDecadesQ16[257];
extern const Unit kSvfShiftQ31[257];
extern const Gain kStiffnessQ29[257];
extern const uint32_t kPitchRatioHighQ16[257];
extern const uint32_t kPitchRatioLowQ30[257];

// 147 logical 48k->44.1k phases are reconstructed from these 74 stored rows.
// For phase >= 74, select row 147-phase and reverse the 256 taps.
extern const int32_t kSrcCoefficientsQ31[74][256];

// Interpolate a source table at an unsigned Q8 coordinate over [0, 256]. The
// endpoint coordinate 256<<8 returns element 256 without reading 257.
uint32_t lookup_u32_q8(const uint32_t table[257], uint32_t index_q8,
                       NumericStats& stats);
int32_t lookup_i32_q8(const int32_t table[257], uint32_t index_q8,
                      NumericStats& stats);

// Evaluate the source's 4096-interval sine cycle at a wrapping Q0.32 phase.
Audio lookup_sine_phase_q32(uint32_t phase, NumericStats& stats);

// The source pitch recipe is high[integer semitones] * low[truncated
// fractional 1/256 semitone], with semitones in signed Q16 and table offset
// +128. The result is an unsigned Q30 ratio held in 64 bits (range up to the
// source's approximately 1626x formula-derived high-table endpoint).
uint64_t semitones_to_ratio_q30(int32_t semitones_q16, NumericStats& stats);
Frequency scale_frequency_q30(Frequency frequency, uint64_t ratio_q30,
                              NumericStats& stats);

// Resolve one valid logical SRC phase to the shared stored row and tap order.
// Returns false and increments invalid_index for phase >= 147.
bool src_coefficient_row(uint32_t phase, const int32_t** row,
                         bool* reverse_taps, NumericStats& stats);

}  // namespace dsp
}  // namespace rings
