#pragma once

#include <opxy/audio.hpp>
#include <opxy/lease_pool.hpp>
#include "model.hpp"
#include "src.hpp"

namespace rings {

struct Voice {
  opxy::u32 lease;
  opxy::u32 strike_pending;
};

static_assert(sizeof(Voice) == 8, "Rings voices are two 32-bit lease words");
static_assert(alignof(Voice) == 4, "Rings voices require 32-bit alignment");

struct Context {
  dsp::Core core;
  src::Q31SrcState src_state;
  dsp::Audio source_cache[dsp::kCoreTickSamples];
  dsp::Patch active_patch;
  dsp::Patch pending_patch;
  dsp::NumericStats src_stats;
  uint32_t active_model;
  uint32_t active_output;
  uint32_t pending_model;
  uint32_t pending_output;
  uint32_t cache_position;
  uint32_t core_tick_frame;
  uint32_t pitch_frame;
  uint32_t pitch_remainder;
  uint32_t callback_frame;
  uint32_t arrival_source_frame;
  uint32_t arrival_remainder;
  uint32_t arrival_tick_offset;
  uint32_t pending_tick_frame;
  uint32_t pending_controls;
  uint32_t last_pitch;
  uint32_t has_last_pitch;
};

struct Shared {
  opxy::LeasePool<Context, 2> contexts;
};

void initialize(Voice&, Shared&, opxy::u32 owner);
void start(Voice&, Shared&, opxy::u32 owner);
void destroy(Voice&, Shared&, opxy::u32 owner);
void render(Voice&, Shared&, opxy::u32 owner, opxy::Output,
            opxy::Parameters, opxy::Pitch);

}  // namespace rings
