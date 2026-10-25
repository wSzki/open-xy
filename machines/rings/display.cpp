#include "display.h"
#include <opxy/q31.hpp>

namespace rings {
namespace {
using opxy::i32;
using opxy::u32;
using namespace opxy::draw;
const i32 expired = 8192; // Clamp one delayed GUI frame to eight Q10 seconds.

// Q10 quarter-wave sine samples; inverse entries cover pluck peaks 2..13.
const i32 quarter_sine[17] = {
    0, 100, 199, 297, 392, 483, 569, 650, 724, 791, 852, 902, 946, 980, 1004, 1019, 1024
};
const i32 inverse_samples[16] = {0, 0, 512, 341, 256, 205, 171, 146, 128, 114, 102, 93, 85, 79, 0, 0};

inline i32 sine64(u32 phase) {
    const u32 quadrant = (phase >> 4) & 3u;
    const u32 offset = phase & 15u;
    const i32 rising = quarter_sine[offset];
    const i32 falling = quarter_sine[16u - offset];
    return quadrant == 0 ? rising : quadrant == 1 ? falling : quadrant == 2 ? -rising : -falling;
}

// Slots keep hidden parameters current; shown selects the live encoder snapshot.
inline void read_controls(const opxy::draw::Input& input, i32 values[4]) {
    for (u32 i = 0; i < 4; ++i) values[i] = clamp(input.slots[i], 0, opxy::q31::maximum);
    for (u32 i = 0; i < 4; ++i) {
        const u32 shown = input.shown[i];
        if (shown < 4) values[shown] = clamp(input.parameters[i], 0, opxy::q31::maximum);
    }
}

inline i32 pluck_profile(u32 sample, u32 peak) {
    const u32 profile = sample <= peak ? sample * static_cast<u32>(inverse_samples[peak])
                                       : (15u - sample) * static_cast<u32>(inverse_samples[15u - peak]);
    return static_cast<i32>(profile > unit ? unit : profile);
}

// Brightness adds weighted second and third string modes to the fundamental.
inline i32 vibration(u32 spatial, u32 time, i32 brightness) {
    const i32 fundamental = floor_q10(sine64(spatial) * sine64(time));
    const i32 second_weight = floor_q10(brightness * 512);
    const i32 brightness_squared = floor_q10(brightness * brightness);
    const i32 third_weight = floor_q10(brightness_squared * 256);
    i32 result = fundamental;
    if (second_weight) {
        const i32 second = floor_q10(sine64((spatial * 2u) & 63u) * sine64((time * 2u) & 63u));
        result += floor_q10(second * second_weight);
    }
    if (third_weight) {
        const i32 third = floor_q10(sine64((spatial * 3u) & 63u) * sine64((time * 3u) & 63u));
        result += floor_q10(third * third_weight);
    }
    return result;
}

inline void set_point(Frame::path_type& path, u32 index, i32 x, i32 y, bool& changed) {
    Point& point = path.points[index];
    if (point.x != x || point.y != y) changed = true;
    point.x = x;
    point.y = y;
}

// One solid stroke per string: horizontal at Structure 0, tilting as Structure
// rises. Brightness scales the centered string span in both axes, preserving
// its tilt. Both outline edges share the same points for one crisp stroke.
inline void string_path(Frame::path_type& path, u32 string, i32 brightness,
                        u32 peak, i32 amplitude, u32 phase, i32 slope) {
    const u32 old_key = path.key;
    bool changed = old_key == 0;
    const u32 time = (phase + string * 6u) & 63u;
    const i32 half_length = 28 + brightness * 27 / 1023;

    for (u32 sample = 0; sample < 16; ++sample) {
        const i32 centered = static_cast<i32>(sample * 2u) - 15;
        const i32 x = half_length * unit * centered / 15;
        const i32 base_y = (slope * unit / 2 - slope * static_cast<i32>(unit * sample) / 15) *
            half_length / 55;
        i32 displacement = 0;
        if (amplitude) {
            const u32 spatial = sample * 64u / 15u;
            const i32 profile = pluck_profile(sample, peak);
            const i32 envelope = floor_q10(amplitude * profile);
            const i32 wave = vibration(spatial, time, brightness);
            displacement = floor_q10(floor_q10(envelope * wave) * (3 * unit));
        }
        const i32 y = base_y + displacement;
        set_point(path, sample, x, y, changed);
        set_point(path, 31u - sample, x, y, changed);
    }

    if (changed) {
        path.key = old_key + 1u;
        if (!path.key) path.key = 1;
    }
}
} // namespace

inline void display_reset(DisplayState& state) {
    state.previous_event = 0;
    state.track = 0;
    state.amplitude = 0;
    state.decay_remainder = 0;
    state.phase = 0;
    state.initialized = 0;
    state.cached_slope = 0;
    state.cached_brightness = 0;
    state.cached_position = 0;
    state.cached_amplitude = 0;
    state.cached_phase = 0;
}

// This renderer consumes one GUI snapshot only; it never touches audio or host services.
inline void display(DisplayState& state, const opxy::draw::Input& input, Frame& frame) {
    i32 controls[4];
    read_controls(input, controls);
    const i32 structure = controls[0] >> 21;
    const i32 brightness = controls[1] >> 21;
    const i32 damping = controls[2] >> 21;
    const i32 position = controls[3] >> 21;
    const bool snapshot = !state.initialized || state.track != input.track;

    // A first frame or track change adopts the serial without inventing a pluck.
    if (snapshot) {
        state.amplitude = 0;
        state.decay_remainder = 0;
        state.phase = 0;
    } else if (!input.reduced_motion && state.amplitude) { // Reduced motion holds note state; controls still shape the lines.
        const i32 elapsed = clamp(input.elapsed, 0, expired);
        // Brief note-triggered decay; this timer-based preview is not PCM.
        const i32 rate = 5700 - ((damping * 4000) >> 10);
        const i32 decay = elapsed * rate + static_cast<i32>(state.decay_remainder);
        const i32 loss = floor_q10(decay);
        state.decay_remainder = static_cast<u32>(decay - loss * unit);
        if (state.amplitude > loss) {
            state.amplitude -= loss;
        } else {
            state.amplitude = 0;
            state.decay_remainder = 0;
        }
        if (state.amplitude) {
            const u32 frequency = 12u + (static_cast<u32>(structure) * 12u >> 10); // 12..24 cycles per second.
            state.phase = (state.phase + static_cast<u32>(elapsed) * frequency) & 1023u;
        }
    }

    if (!snapshot && input.track < 8 && input.note_serial != state.previous_event) {
        state.amplitude = unit;
        state.decay_remainder = 0;
        state.phase = 256;
    }
    state.track = input.track;
    state.previous_event = input.note_serial;
    state.initialized = 1;

    // Structure tilts the strings from horizontal; Position spaces the group.
    const i32 slope = static_cast<i32>(static_cast<u32>(structure) * 40u >> 10);
    const i32 gap = 18 + static_cast<i32>(static_cast<u32>(position) * 28u >> 10);
    // Damping shades the stroke from dark grey at 0 toward white at 100; Brightness lengthens it.
    const u32 shade = static_cast<u32>(damping) * 5u >> 10;
    // Position selects a triangular pluck profile between interior samples 2..13.
    const u32 peak = state.amplitude ? 2u + (static_cast<u32>(position) * 12u >> 10) : 0;
    const u32 wave_phase = state.amplitude ? state.phase >> 4 : 0;
    // Cache the exact quantized geometry inputs; steady strings skip all path work.
    // A fresh frame (key 0) is always written: the first call after reset sets every path.
    const bool geometry_changed = !frame.paths[0].key || state.cached_slope != slope ||
        state.cached_brightness != brightness || state.cached_position != static_cast<i32>(peak) ||
        state.cached_amplitude != state.amplitude || state.cached_phase != wave_phase;
    if (geometry_changed) {
        for (u32 string = 0; string < Frame::path_count; ++string)
            string_path(frame.paths[string], string, brightness, peak, state.amplitude, wave_phase, slope);
        state.cached_slope = slope;
        state.cached_brightness = brightness;
        state.cached_position = static_cast<i32>(peak);
        state.cached_amplitude = state.amplitude;
        state.cached_phase = wave_phase;
    }
    for (u32 string = 0; string < Frame::path_count; ++string) {
        const i32 y = 120 + (static_cast<i32>(string) - 1) * gap;
        place(frame, string, 240 * unit, y * unit, 2 * unit, shade, fill_and_stroke);
    }
}
} // namespace rings
