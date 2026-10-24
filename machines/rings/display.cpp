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

// One solid stroke per string: a steep "/" diagonal whose ends are anchored.
// Both outline edges share the same points, so the closed strip renders as a
// single crisp line instead of a blurred double edge.
inline void string_path(Frame::path_type& path, u32 string, i32 brightness,
                        u32 peak, i32 amplitude, u32 phase) {
    const u32 old_key = path.key;
    bool changed = old_key == 0;
    const u32 time = (phase + string * 6u) & 63u;

    for (u32 sample = 0; sample < 16; ++sample) {
        const i32 x = -15 * unit + static_cast<i32>(sample * 31u * unit / 15u);
        const i32 y = 48 * unit - static_cast<i32>(sample * 96u * unit / 15u);
        i32 displacement = 0;
        if (amplitude) {
            const u32 spatial = sample * 64u / 15u;
            const i32 profile = pluck_profile(sample, peak);
            const i32 envelope = floor_q10(amplitude * profile);
            const i32 wave = vibration(spatial, time, brightness);
            displacement = floor_q10(floor_q10(envelope * wave) * (3 * unit));
        }
        set_point(path, sample, x + displacement, y, changed);
        set_point(path, 31u - sample, x + displacement, y, changed);
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
        // Visible ring-out stays inside the audible decay: about 0.45 s at
        // minimum Damping and 1.3 s at maximum, instead of outlasting the sound.
        const i32 rate = 2304 - ((damping * 1536) >> 10);
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
            const u32 frequency = 3u + (static_cast<u32>(structure) * 10u >> 10); // 3..13 cycles per second.
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

    // Position tilts the pluck point and shifts the whole group sideways.
    const i32 shift = static_cast<i32>(static_cast<u32>(position) * 12u >> 10) - 6;
    // Position selects a triangular pluck profile between interior samples 2..13.
    const u32 peak = state.amplitude ? 2u + (static_cast<u32>(position) * 12u >> 10) : 0;
    const u32 wave_phase = state.amplitude ? state.phase >> 4 : 0;
    // Cache the exact quantized geometry inputs; steady strings skip all path work.
    // Structure only drives vibration frequency, so it never rebuilds resting geometry.
    // A fresh frame (key 0) is always written: the first call after reset sets every path.
    const bool geometry_changed = !frame.paths[0].key || state.cached_brightness != brightness ||
        state.cached_position != static_cast<i32>(peak) || state.cached_amplitude != state.amplitude ||
        state.cached_phase != wave_phase;
    if (geometry_changed) {
        for (u32 string = 0; string < Frame::path_count; ++string)
            string_path(frame.paths[string], string, brightness, peak, state.amplitude, wave_phase);
        state.cached_brightness = brightness;
        state.cached_position = static_cast<i32>(peak);
        state.cached_amplitude = state.amplitude;
        state.cached_phase = wave_phase;
    }
    for (u32 string = 0; string < Frame::path_count; ++string) {
        const i32 x = 240 + (static_cast<i32>(string) - 1) * 76 + shift;
        place(frame, string, x * unit, 120 * unit, 2 * unit, 0, fill_and_stroke);
    }
}
} // namespace rings
