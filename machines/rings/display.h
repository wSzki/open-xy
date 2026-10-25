#pragma once
#include <opxy/draw.hpp>

namespace rings {
// Palette has five Damping shades; Brightness controls string length.
using Frame = opxy::draw::Frame<3, 32, 3, 5>;
struct DisplayState {
    opxy::u32 previous_event;
    opxy::u32 track;
    opxy::i32 amplitude;
    opxy::u32 decay_remainder;
    opxy::u32 phase;
    opxy::u32 initialized;
    // Cached inputs let resting frames bypass mutable-path generation.
    opxy::i32 cached_slope;
    opxy::i32 cached_brightness;
    opxy::i32 cached_position;
    opxy::i32 cached_amplitude;
    opxy::u32 cached_phase;
};
static_assert(sizeof(DisplayState) == 44 && alignof(DisplayState) == 4, "Rings display state resource");
}  // namespace rings
