#pragma once

#include "dsp.hpp"

namespace rings {
namespace dsp {

static const unsigned kCoreTickSamples = 24;
static const unsigned kModalBankModes = 60;
static const unsigned kStringBankVoices = 8;
static const unsigned kStringDelayLength = 2048;
static const unsigned kStringStretchDelayLength = 1024;
static const unsigned kPluckDelayLength = 256;

struct SvfState {
  Audio s1;
  Audio s2;
  Unit h;
  Unit a2;
  Unit a3;
};
struct LfoState {
  Audio y0;
  Audio y1;
  Audio initial_amplitude;
  Gain coefficient;
};

struct ModalModeState {
  SvfState filter;
  Frequency frequency;
  uint32_t q_q8;
};

struct ModalBankState {
  ModalModeState modes[kModalBankModes];
  Frequency base_frequency;
  Unit previous_position;
  Unit structure;
  Unit brightness;
  Unit damping;
  Unit position;
  unsigned mode_count;
};

struct StringBankState {
  Audio delay[kStringDelayLength];
  Audio stretch_delay[kStringStretchDelayLength];
  SvfState iir_filter;
  Audio fir_x1;
  Audio fir_x2;
  Audio dc_state;
  Audio dc_previous_input;
  Audio dispersion_noise;
  Audio curved_bridge;
  Audio out_previous;
  Audio out_current;
  Audio aux_previous;
  Audio aux_current;
  Delay delay_samples;
  Delay previous_delay;
  Unit previous_position;
  int32_t previous_dispersion;
  Unit previous_damping_compensation;
  Frequency frequency;
  int32_t dispersion;
  Unit brightness;
  Unit damping;
  Unit position;
  Unit fir_damping;
  Unit fir_brightness;
  int32_t fir_damping_step;
  int32_t fir_brightness_step;
  Gain dispersion_gain;
  unsigned delay_index;
  unsigned stretch_index;
  Unit src_phase;
  Unit src_ratio;
  unsigned initialized;
  LfoState lfo;
};

struct PluckerState {
  Audio delay[kPluckDelayLength];
  SvfState filter;
  unsigned remaining_samples;
  unsigned delay_index;
  Delay comb_period;
  Gain comb_gain;
};

struct ExciterState {
  SvfState filter;
  Audio dc_state;
  Audio previous_input;
  Unit dc_coefficient;
};

struct LimiterState {
  Audio envelope;
};

class Core {
 public:
  // The SDK lease pool must have byte-cleared this POD state first. This sets
  // lease metadata only; delay-line and filter histories are not re-cleared.
  void initialize_zeroed();
  void render_tick(Frequency frequency, const Patch& patch, unsigned model,
                   bool strike, Audio out[kCoreTickSamples],
                   Audio aux[kCoreTickSamples]);
  const NumericStats& stats() const { return stats_; }

 private:
  void reset_modal_history();
  void reset_string_history(unsigned index);
  void configure_modal(Frequency frequency, const Patch& patch);
  void render_modal(Frequency frequency, const Patch& patch, bool strike,
                    Audio out[kCoreTickSamples], Audio aux[kCoreTickSamples]);
  void render_sympathetic(Frequency frequency, const Patch& patch, bool strike,
                          Audio out[kCoreTickSamples], Audio aux[kCoreTickSamples]);
  void render_dispersive(Frequency frequency, const Patch& patch, bool strike,
                         Audio out[kCoreTickSamples], Audio aux[kCoreTickSamples]);
  void render_string_bank(Frequency frequency, const Patch& patch, bool strike,
                          bool dispersion, bool sympathetic,
                          Audio out[kCoreTickSamples],
                          Audio aux[kCoreTickSamples]);
  void render_limiter(Audio out[kCoreTickSamples], Audio aux[kCoreTickSamples]);

  ModalBankState modal_;
  StringBankState strings_[kStringBankVoices];
  PluckerState pluckers_[kStringBankVoices];
  ExciterState exciter_;
  LimiterState limiter_;
  Audio input_[kCoreTickSamples];
  Audio sympathetic_input_[kCoreTickSamples];
  Audio string_out_[kCoreTickSamples];
  Audio string_aux_[kCoreTickSamples];
  NumericStats stats_;
  uint32_t rng_state_;
  unsigned active_model_;
  unsigned strike_pending_;
};

}  // namespace dsp
}  // namespace rings

