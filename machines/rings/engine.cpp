#include "core.h"

namespace rings {
namespace {

static const uint32_t kOutputCycleFrames = 28224u;  // 441 callbacks * 64.
static const uint32_t kSourceCycleFrames = 30720u;  // 1280 ticks * 24.
static const uint32_t kCallbackSourceStepWhole = 69u;
static const uint32_t kCallbackSourceStepRemainder = 97u;  // 10240 mod 147.
static const uint32_t kTickPitchStepWhole = 22u;
static const uint32_t kTickPitchStepRemainder = 8u;  // 24 * 147 mod 160.
static const uint32_t kTickPitchDenominator = 160u;
static const uint32_t kMaxInputFramesPerCallback = 71u;
static const int64_t kQ31HalfscaleLimit = INT64_C(1073741823);

static inline uint32_t EnumIndex(opxy::i32 value) {
  if (value < 0) {
    return 0u;
  }
  const uint32_t q15 = static_cast<uint32_t>(value) >> 16;
  const uint32_t index = (q15 * 3u) >> 15;
  return index < 3u ? index : 2u;
}

static inline opxy::u32 UnitFromControl(opxy::i32 value) {
  return value < 0 ? 0u : static_cast<opxy::u32>(value);
}

static inline dsp::Patch DecodePatch(opxy::Parameters parameters) {
  dsp::Patch patch;
  patch.structure = UnitFromControl(parameters[0]);
  patch.brightness = UnitFromControl(parameters[1]);
  patch.damping = UnitFromControl(parameters[2]);
  patch.position = UnitFromControl(parameters[3]);
  return patch;
}

static inline uint32_t ScheduledTick(const Context& context) {
  if (context.arrival_tick_offset == 0u &&
      context.arrival_remainder == 0u) {
    return context.arrival_source_frame;
  }
  uint32_t tick = context.arrival_source_frame +
      (24u - context.arrival_tick_offset);
  return tick == kSourceCycleFrames ? 0u : tick;
}

static inline uint32_t ClampEventToNextTick(uint32_t requested,
                                            const Context& context) {
  const uint32_t distance_behind = context.core_tick_frame >= requested
      ? context.core_tick_frame - requested
      : context.core_tick_frame + kSourceCycleFrames - requested;
  return distance_behind != 0u &&
                 distance_behind < kSourceCycleFrames / 2u
      ? context.core_tick_frame
      : requested;
}

static inline void QueueSnapshot(Context& context,
                                 opxy::Parameters parameters) {
  context.pending_patch = DecodePatch(parameters);
  context.pending_model = EnumIndex(parameters[4]);
  context.pending_output = EnumIndex(parameters[5]);
  context.pending_tick_frame =
      ClampEventToNextTick(ScheduledTick(context), context);
  context.pending_controls = 1u;
}

static inline uint32_t PitchWordForTick(const Context& context,
                                        opxy::Pitch pitch) {
  const uint32_t forward = context.pitch_frame >= context.callback_frame
      ? context.pitch_frame - context.callback_frame
      : context.pitch_frame + kOutputCycleFrames - context.callback_frame;
  if (forward < opxy::block_frames) {
    return pitch[forward];
  }

  const bool tick_before_arrival =
      context.core_tick_frame < context.arrival_source_frame ||
      (context.core_tick_frame == context.arrival_source_frame &&
       context.arrival_remainder != 0u);
  if (tick_before_arrival) {
    return context.has_last_pitch ? context.last_pitch : pitch[0];
  }

  // The cursor can reach the end of the current block only through the
  // bounded tick/cache schedule. Hold the last available word; never read a
  // future callback's pitch array.
  return pitch[opxy::block_frames - 1u];
}

static inline int32_t ClampSourceQ31(int64_t value) {
  if (value > kQ31HalfscaleLimit) {
    return static_cast<int32_t>(kQ31HalfscaleLimit);
  }
  if (value < -kQ31HalfscaleLimit) {
    return static_cast<int32_t>(-kQ31HalfscaleLimit);
  }
  return static_cast<int32_t>(value);
}

static inline void AdvanceTickCursors(Context& context) {
  context.core_tick_frame += dsp::kCoreTickSamples;
  if (context.core_tick_frame == kSourceCycleFrames) {
    context.core_tick_frame = 0u;
  }

  context.pitch_frame += kTickPitchStepWhole;
  context.pitch_remainder += kTickPitchStepRemainder;
  if (context.pitch_remainder >= kTickPitchDenominator) {
    context.pitch_frame += 1u;
    context.pitch_remainder -= kTickPitchDenominator;
  }
  if (context.pitch_frame == kOutputCycleFrames) {
    context.pitch_frame = 0u;
  }
}

static inline void RenderTick(Context& context, Voice& voice,
                              opxy::Pitch pitch, dsp::Audio right[24]) {
  bool strike = false;
  if (context.pending_controls &&
      context.core_tick_frame == context.pending_tick_frame) {
    context.active_patch = context.pending_patch;
    context.active_model = context.pending_model;
    context.active_output = context.pending_output;
    context.pending_controls = 0u;
    strike = voice.strike_pending != 0u;
    voice.strike_pending = 0u;
  }

  const uint32_t raw_pitch = PitchWordForTick(context, pitch);
  const dsp::Frequency frequency =
      dsp::sdk_frequency(raw_pitch, context.src_stats);
  context.core.render_tick(frequency, context.active_patch,
                           context.active_model, strike,
                           context.source_cache, right);

  for (uint32_t i = 0; i < dsp::kCoreTickSamples; ++i) {
    const int64_t left = context.source_cache[i];
    const int64_t auxiliary = right[i];
    int64_t selected;
    if (context.active_output == 0u) {
      selected = left * 32;
    } else if (context.active_output == 1u) {
      selected = (left + auxiliary) * 16;
    } else {
      selected = auxiliary * 32;
    }
    context.source_cache[i] = ClampSourceQ31(selected);
  }
  AdvanceTickCursors(context);
}

static inline void AdvanceCallbackCursors(Context& context) {
  context.callback_frame += opxy::block_frames;
  if (context.callback_frame == kOutputCycleFrames) {
    context.callback_frame = 0u;
  }

  uint32_t source_step = kCallbackSourceStepWhole;
  context.arrival_remainder += kCallbackSourceStepRemainder;
  if (context.arrival_remainder >= src::kPhaseCount) {
    context.arrival_remainder -= src::kPhaseCount;
    ++source_step;
  }
  context.arrival_source_frame += source_step;
  context.arrival_tick_offset += source_step;
  while (context.arrival_tick_offset >= dsp::kCoreTickSamples) {
    context.arrival_tick_offset -= dsp::kCoreTickSamples;
  }
  if (context.arrival_source_frame == kSourceCycleFrames) {
    context.arrival_source_frame = 0u;
    context.arrival_remainder = 0u;
    context.arrival_tick_offset = 0u;
  }
}

static inline void InitializeNewContext(Context& context) {
  // LeasePool has already byte-cleared the complete Context. Core initialization
  // sets only its required nonzero defaults; the SRC history and counters stay
  // zero without a second clear.
  context.core.initialize_zeroed();
  context.cache_position = dsp::kCoreTickSamples;
  context.active_model = 0u;
  context.active_output = 0u;
  context.pending_model = 0u;
  context.pending_output = 0u;
  context.pending_controls = 0u;
  context.core_tick_frame = 0u;
  context.pitch_frame = 0u;
  context.pitch_remainder = 0u;
  context.callback_frame = 0u;
  context.arrival_source_frame = 0u;
  context.arrival_remainder = 0u;
  context.arrival_tick_offset = 0u;
  context.pending_tick_frame = 0u;
  context.last_pitch = 0u;
  context.has_last_pitch = 0u;
}

static inline void ZeroBlock(opxy::Output output) {
  for (uint32_t i = 0; i < opxy::block_frames; ++i) {
    output[i] = 0;
  }
}

}  // namespace

void initialize(Voice& voice, Shared& shared, opxy::u32 owner) {
  shared.contexts.release(owner, voice.lease);
  voice.lease = 0u;
  voice.strike_pending = 0u;
}

void start(Voice& voice, Shared& shared, opxy::u32 owner) {
  const bool already_owned =
      shared.contexts.get(owner, voice.lease) != 0;
  voice.lease = shared.contexts.acquire(owner, voice.lease);
  Context* context = shared.contexts.get(owner, voice.lease);
  if (!already_owned && context) {
    InitializeNewContext(*context);
  }
  voice.strike_pending = 1u;
}

void destroy(Voice& voice, Shared& shared, opxy::u32 owner) {
  shared.contexts.release(owner, voice.lease);
  voice.lease = 0u;
  voice.strike_pending = 0u;
}

void render(Voice& voice, Shared& shared, opxy::u32 owner,
            opxy::Output output, opxy::Parameters parameters,
            opxy::Pitch pitch) {
  Context* context = shared.contexts.get(owner, voice.lease);
  if (!context) {
    voice.lease = 0u;
    voice.strike_pending = 0u;
    ZeroBlock(output);
    return;
  }

  if (context->src_stats.internal_saturation != 0u ||
      context->src_stats.invalid_division != 0u ||
      context->src_stats.invalid_range != 0u ||
      context->src_stats.invalid_index != 0u) {
    ZeroBlock(output);
    return;
  }

  QueueSnapshot(*context, parameters);
  dsp::Audio right[dsp::kCoreTickSamples];
  uint32_t output_frames = 0u;
  uint32_t input_frames = 0u;
  bool src_fault = false;
  while (output_frames < opxy::block_frames &&
         input_frames < kMaxInputFramesPerCallback) {
    if (context->cache_position == dsp::kCoreTickSamples) {
      RenderTick(*context, voice, pitch, right);
      context->cache_position = 0u;
    }

    const int32_t input = context->source_cache[context->cache_position++];
    src::Q31SrcOutput converted;
    const src::PushResult result = src::PushQ31Src(
        &context->src_state, input, context->src_stats, &converted);
    ++input_frames;
    if (result == src::kOutput) {
      output[output_frames++] = converted.clamped;
    } else if (result == src::kInvalidPhase) {
      src_fault = true;
      break;
    }
  }

  if (output_frames < opxy::block_frames) {
    if (!src_fault) {
      dsp::note_invalid_range(context->src_stats);
    }
    while (output_frames < opxy::block_frames) {
      output[output_frames++] = 0;
    }
  }

  context->last_pitch = pitch[opxy::block_frames - 1u];
  context->has_last_pitch = 1u;
  AdvanceCallbackCursors(*context);
}

}  // namespace rings
