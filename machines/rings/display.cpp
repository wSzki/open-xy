// Retained Rings segment display: four model characters, four amount dials,
// and the currently selected alternate control value.
#include <opxy/segments.hpp>

namespace rings {
using Frame = opxy::segments::Frame<4>;

static const opxy::i32 kSweep = 1440;
static const unsigned kStructureSlot = 0;
static const unsigned kBrightnessSlot = 1;
static const unsigned kDampingSlot = 2;
static const unsigned kPositionSlot = 3;
static const unsigned kModelSlot = 4;
static const unsigned kOutputSlot = 5;

static const char* const kCodes[] = {"MODL", "SYMP", "INHR"};
static const char* const kLabels[] = {"MODAL", "SYMPATHETIC", "INHARMONIC"};
static const char* const kOutputs[] = {"OUT", "MIX", "AUX"};

// This is the same integer Q31 -> Q15 enum quantizer as Macro Oscillator.
static inline unsigned enumIndex(opxy::i32 value, unsigned count) {
  const unsigned index = value < 0 ? 0 : ((static_cast<unsigned>(value) >> 16) * count) >> 15;
  return index < count ? index : count - 1;
}

static inline opxy::u32 glyph(char c) {
  const opxy::u32 a = 1u << 15, b = 1u << 14, cc = 1u << 13, d = 1u << 12;
  const opxy::u32 e = 1u << 11, f = 1u << 10, g = 1u << 9, h = 1u << 8;
  const opxy::u32 j = 1u << 7, k = 1u << 6, p = 1u << 2;
  switch (c) {
    case 'M': return b | cc | e | f | h | j;
    case 'O': return a | b | cc | d | e | f;
    case 'D': return a | b | cc | d | e | f | j;
    case 'L': return d | e | f;
    case 'S': return a | cc | d | f | g | h;
    case 'Y': return b | cc | d | f | g | k;
    case 'P': return a | b | e | f | g | h;
    case 'A': return a | b | cc | e | f | g | h | j;
    case 'T': return a | k | p;
    case 'H': return b | cc | e | f | g | h | j;
    case 'I': return a | d | k | p;
    case 'N': return b | cc | e | f | h | j;
    case 'R': return a | b | e | f | g | h | j | k;
    default: return 0;
  }
}

static inline void modelMasks(unsigned model, Frame& frame) {
  const char* code = kCodes[model];
  for (unsigned i = 0; i < 4; ++i) frame.masks[i] = glyph(code[i]);
}
static inline void amount(char* out, unsigned capacity, opxy::i32 value) {
  const unsigned tenths = value < 0 ? 0 : ((static_cast<unsigned>(value) >> 10) * 1000 + (1u << 20)) >> 21;
  char text[6];
  unsigned n = 0;
  if (tenths >= 1000) text[n++] = '1';
  if (tenths >= 100) text[n++] = static_cast<char>('0' + tenths / 100 % 10);
  text[n++] = static_cast<char>('0' + tenths / 10 % 10);
  text[n++] = '.';
  text[n++] = static_cast<char>('0' + tenths % 10);
  text[n] = 0;
  opxy::segments::text(out, capacity, text);
}

inline void display(const opxy::segments::Input& input, Frame& frame) {
  const unsigned model = enumIndex(input.slots[kModelSlot], 3);
  modelMasks(model, frame);
  opxy::segments::text(frame.label, sizeof(frame.label), kLabels[model]);
  frame.key = model + 1;

  for (unsigned i = 0; i < 4; ++i) {
    const unsigned shown = input.shown[i];
    if (shown == kModelSlot) {
      opxy::segments::text(frame.values[i], sizeof(frame.values[i]), kCodes[model]);
    } else if (shown == kOutputSlot) {
      const unsigned output = enumIndex(input.slots[kOutputSlot], 3);
      opxy::segments::text(frame.values[i], sizeof(frame.values[i]), kOutputs[output]);
    } else {
      amount(frame.values[i], sizeof(frame.values[i]), input.parameters[i]);
    }
  }

  const unsigned primary[] = {kStructureSlot, kBrightnessSlot, kDampingSlot, kPositionSlot};
  for (unsigned i = 0; i < 4; ++i)
    frame.dials[i] = opxy::segments::sweep(input.slots[primary[i]], kSweep);
}
}  // namespace rings
