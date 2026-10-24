#!/usr/bin/env python3
"""Generate the selected integer Rings lookup tables into machines/rings/tables.cpp.

Inputs are the pinned, local source closure and the already-qualified 147/160
Q31 SRC coefficients. This script uses only Python's standard library and
does not download, install or execute external tooling. Run with
`python3 machines/rings/tools/generate_tables.py` from the checkout root.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_RESOURCES = ROOT / "local/rings-night-001/validation/reference/rings/resources.cc"
DEFAULT_UNITS = ROOT / "local/rings-night-001/validation/reference/stmlib/dsp/units.cc"
DEFAULT_SRC_Q31 = ROOT / "local/rings-night-001/validation/src-feasibility/coefficients_q31.hpp"
DEFAULT_OUTPUT = ROOT / "machines/rings/tables.cpp"

FLOAT_TOKEN = re.compile(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?")
INT_TOKEN = re.compile(r"[+-]?\d+")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_array(path: Path, name: str) -> list[float]:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        rf"const\s+float\s+{re.escape(name)}\s*\[\s*\]\s*=\s*\{{(.*?)\}}\s*;",
        text,
        re.DOTALL,
    )
    if match is None:
        raise ValueError(f"missing float array {name} in {path}")
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
    tokens = FLOAT_TOKEN.findall(body)
    if re.sub(r"[\s,]", "", FLOAT_TOKEN.sub("", body)):
        raise ValueError(f"unexpected syntax in float array {name} in {path}")
    if not tokens:
        raise ValueError(f"empty float array {name} in {path}")
    values = [float(token) for token in tokens]
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f"non-finite source value in {name} in {path}")
    return values


def source_q31_coefficients(path: Path) -> list[list[int]]:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        r"static\s+const\s+int32_t\s+kCoefficientsQ31\s*\[\s*74\s*\]"
        r"\s*\[\s*256\s*\]\s*=\s*\{(.*?)\n\};",
        text,
        re.DOTALL,
    )
    if match is None:
        raise ValueError(f"missing 74x256 Q31 coefficient tensor in {path}")
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", match.group(1), flags=re.DOTALL)
    tokens = INT_TOKEN.findall(body)
    if re.sub(r"[\s,{}]", "", INT_TOKEN.sub("", body)):
        raise ValueError(f"unexpected syntax in Q31 coefficient tensor in {path}")
    values = [int(token) for token in tokens]
    if len(values) != 74 * 256:
        raise ValueError(f"expected 18944 Q31 taps, found {len(values)} in {path}")
    if any(value < -(1 << 31) or value > (1 << 31) - 1 for value in values):
        raise ValueError(f"out-of-range signed Q31 coefficient in {path}")
    rows = [values[offset : offset + 256] for offset in range(0, len(values), 256)]
    if any(sum(row) != (1 << 31) for row in rows):
        raise ValueError(f"a stored Q31 SRC phase does not sum to unity in {path}")
    return rows


def binary32(value: float) -> float:
    """Interpret a source decimal as the float value used by the C++ tables."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def round_away(value: float) -> int:
    if value < 0.0:
        return -int(math.floor(-value + 0.5))
    return int(math.floor(value + 0.5))


def quantize(values: list[float], fractional_bits: int, *, signed: bool,
             bits: int, name: str) -> list[int]:
    scale = 1 << fractional_bits
    result = [round_away(binary32(value) * scale) for value in values]
    minimum = -(1 << (bits - 1)) if signed else 0
    maximum = (1 << (bits - (1 if signed else 0))) - 1
    if any(value < minimum or value > maximum for value in result):
        raise ValueError(f"quantized {name} does not fit its declared integer type")
    return result


def require_count(values: list[int], expected: int, name: str) -> None:
    if len(values) != expected:
        raise ValueError(f"{name}: expected {expected} entries, got {len(values)}")


def emit_1d(name: str, ctype: str, values: list[int], per_line: int = 8) -> str:
    lines = [f"const {ctype} {name}[{len(values)}] = {{"]
    for start in range(0, len(values), per_line):
        row = values[start : start + per_line]
        lines.append("  " + ", ".join(str(value) for value in row) + ",")
    lines.append("};")
    return "\n".join(lines)


def emit_2d(name: str, ctype: str, rows: list[list[int]],
            per_line: int = 8) -> str:
    width = len(rows[0])
    lines = [f"const {ctype} {name}[{len(rows)}][{width}] = {{"]
    for row in rows:
        lines.append("  {")
        for start in range(0, width, per_line):
            lines.append("    " + ", ".join(str(value) for value in row[start : start + per_line]) + ",")
        lines.append("  },")
    lines.append("};")
    return "\n".join(lines)


def emit_cpp(resources: Path, units: Path, src_q31: Path) -> str:
    sine = quantize(source_array(resources, "lut_sine"), 25, signed=True,
                    bits=32, name="lut_sine/Q25")
    decades = quantize(source_array(resources, "lut_4_decades"), 16,
                       signed=False, bits=32, name="lut_4_decades/Q16")
    svf_shift = quantize(source_array(resources, "lut_svf_shift"), 31,
                         signed=False, bits=32, name="lut_svf_shift/Q31")
    stiffness = quantize(source_array(resources, "lut_stiffness"), 29,
                         signed=True, bits=32, name="lut_stiffness/Q29")
    pitch_high_source = source_array(units, "lut_pitch_ratio_high")
    pitch_low_source = source_array(units, "lut_pitch_ratio_low")
    if len(pitch_high_source) != 256 or len(pitch_low_source) != 256:
        raise ValueError("expected 256 literal entries in each source pitch table")
    # The pinned source generator emits 256 points, while units.h advertises
    # 257-entry arrays. Extend each table by its source formula's next value so
    # endpoint-safe interpolation has a real final sample.
    pitch_high_source.append(math.pow(2.0, 128.0 / 12.0))
    pitch_low_source.append(math.pow(2.0, 1.0 / 12.0))
    pitch_high = quantize(pitch_high_source, 16, signed=False, bits=32,
                          name="lut_pitch_ratio_high/Q16")
    pitch_low = quantize(pitch_low_source, 30, signed=False, bits=32,
                         name="lut_pitch_ratio_low/Q30")
    src_rows = source_q31_coefficients(src_q31)
    q31_source = src_q31.read_text(encoding="utf-8")
    coefficient_json_hash = re.search(
        r"^// coefficients\.json SHA-256: ([0-9a-f]{64})$",
        q31_source,
        re.MULTILINE,
    )
    logical_coefficients_hash = re.search(
        r"^// Logical binary64 coefficient tensor SHA-256: ([0-9a-f]{64})$",
        q31_source,
        re.MULTILINE,
    )
    if coefficient_json_hash is None or logical_coefficients_hash is None:
        raise ValueError(f"missing coefficient provenance in {src_q31}")

    require_count(sine, 5121, "lut_sine")
    require_count(decades, 257, "lut_4_decades")
    require_count(svf_shift, 257, "lut_svf_shift")
    require_count(stiffness, 257, "lut_stiffness")
    require_count(pitch_high, 257, "lut_pitch_ratio_high")
    require_count(pitch_low, 257, "lut_pitch_ratio_low")

    provenance = [
        "// Generated by machines/rings/tools/generate_tables.py; do not hand-edit.",
        "// Copyright 2015 Emilie Gillet.",
        "// Copyright 2014 Emilie Gillet.",
        "//",
        "// Permission is hereby granted, free of charge, to any person obtaining a copy",
        "// of this software and associated documentation files (the \"Software\"), to deal",
        "// in the Software without restriction, including without limitation the rights",
        "// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell",
        "// copies of the Software, and to permit persons to whom the Software is",
        "// furnished to do so, subject to the following conditions:",
        "//",
        "// The above copyright notice and this permission notice shall be included in",
        "// all copies or substantial portions of the Software.",
        "//",
        "// THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR",
        "// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,",
        "// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE",
        "// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER",
        "// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,",
        "// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN",
        "// THE SOFTWARE.",
        "//",
        "// See http://creativecommons.org/licenses/MIT/ for more information.",
        "//",
        "// Selected pinned source closure (SHA-256):",
        "// Rings source revision: f4cfa7697095a28d292bff758beb51deb49b5a86.",
        "// stmlib source revision: e3bd7c9cc00e4364166f9905c0509b6ffd0535ec.",
        f"//   {resources.relative_to(ROOT).as_posix()}: {sha256(resources)}",
        f"//   {units.relative_to(ROOT).as_posix()}: {sha256(units)}",
        f"//   {src_q31.relative_to(ROOT).as_posix()}: {sha256(src_q31)}",
        f"//   coefficients.json: {coefficient_json_hash.group(1)}",
        f"//   logical binary64 SRC tensor: {logical_coefficients_hash.group(1)}",
        "// Float tables are parsed as source decimals, rounded to IEEE-754 binary32,",
        "// then quantized to the declared Q scale with nearest ties away from zero.",
        "// Pitch-ratio sources have 256 literal entries; their formula-derived",
        "// next samples complete the 257-entry arrays advertised by units.h.",
        "// Q31 SRC coefficients preserve the proven 74x256 symmetric stored rows.",
        "",
    ]
    definitions = [
        emit_1d("kSineQ25", "Audio", sine, per_line=8),
        emit_1d("kFourDecadesQ16", "uint32_t", decades, per_line=8),
        emit_1d("kSvfShiftQ31", "Unit", svf_shift, per_line=8),
        emit_1d("kStiffnessQ29", "Gain", stiffness, per_line=8),
        emit_1d("kPitchRatioHighQ16", "uint32_t", pitch_high, per_line=8),
        emit_1d("kPitchRatioLowQ30", "uint32_t", pitch_low, per_line=8),
        emit_2d("kSrcCoefficientsQ31", "int32_t", src_rows, per_line=8),
    ]


    runtime = r"""
uint32_t lookup_u32_q8(const uint32_t table[257], uint32_t index_q8,
                       NumericStats& stats) {
  if (table == 0) {
    note_invalid_index(stats);
    return 0u;
  }
  if (index_q8 > (256u << 8u)) {
    note_invalid_range(stats);
    return table[256];
  }
  const uint32_t index = index_q8 >> 8u;
  const uint32_t fraction = index_q8 & 0xffu;
  if (index == 256u || fraction == 0u) {
    return table[index];
  }
  const int64_t first_weight = static_cast<int64_t>(256u - fraction);
  const int64_t weighted = static_cast<int64_t>(table[index]) * first_weight +
                           static_cast<int64_t>(table[index + 1u]) *
                               static_cast<int64_t>(fraction);
  const int64_t value = round_shift(weighted, 8u, stats);
  if (value < 0) {
    note_invalid_range(stats);
    return 0u;
  }
  if (static_cast<uint64_t>(value) > UINT32_MAX) {
    note_invalid_range(stats);
    return UINT32_MAX;
  }
  return static_cast<uint32_t>(value);
}

int32_t lookup_i32_q8(const int32_t table[257], uint32_t index_q8,
                      NumericStats& stats) {
  if (table == 0) {
    note_invalid_index(stats);
    return 0;
  }
  if (index_q8 > (256u << 8u)) {
    note_invalid_range(stats);
    return table[256];
  }
  const uint32_t index = index_q8 >> 8u;
  const uint32_t fraction = index_q8 & 0xffu;
  if (index == 256u || fraction == 0u) {
    return table[index];
  }
  const int64_t first_weight = static_cast<int64_t>(256u - fraction);
  const int64_t weighted = static_cast<int64_t>(table[index]) * first_weight +
                           static_cast<int64_t>(table[index + 1u]) *
                               static_cast<int64_t>(fraction);
  return narrow_i32(round_shift(weighted, 8u, stats), stats);
}

Audio lookup_sine_phase_q32(uint32_t phase, NumericStats& stats) {
  const uint64_t scaled = static_cast<uint64_t>(phase) * 4096u;
  const uint32_t index = static_cast<uint32_t>(scaled >> 32u);
  const uint32_t fraction = static_cast<uint32_t>(scaled);
  const int64_t first_weight =
      static_cast<int64_t>((UINT64_C(1) << 32u) - fraction);
  const int64_t weighted =
      static_cast<int64_t>(kSineQ25[index]) * first_weight +
      static_cast<int64_t>(kSineQ25[index + 1u]) *
          static_cast<int64_t>(fraction);
  return narrow_i32(round_shift(weighted, 32u, stats), stats);
}


uint64_t semitones_to_ratio_q30(int32_t semitones_q16, NumericStats& stats) {
  const int32_t minimum = -(INT32_C(128) << 16u);
  const int32_t maximum = INT32_C(128) << 16u;
  if (semitones_q16 < minimum) {
    note_invalid_range(stats);
    semitones_q16 = minimum;
  } else if (semitones_q16 > maximum) {
    note_invalid_range(stats);
    semitones_q16 = maximum;
  }
  const uint64_t offset = static_cast<uint64_t>(
      static_cast<int64_t>(semitones_q16) - minimum);
  const uint32_t integral = static_cast<uint32_t>(offset >> 16u);
  const uint32_t fractional_index =
      static_cast<uint32_t>((offset & 0xffffu) >> 8u);
  const uint32_t high = lookup_u32_q8(kPitchRatioHighQ16,
                                      integral << 8u, stats);
  const uint32_t low = lookup_u32_q8(kPitchRatioLowQ30,
                                     fractional_index << 8u, stats);
  const uint64_t product = static_cast<uint64_t>(high) * low;
  return static_cast<uint64_t>(round_shift(
      static_cast<int64_t>(product), 16u, stats));
}

Frequency scale_frequency_q30(Frequency frequency, uint64_t ratio_q30,
                              NumericStats& stats) {
  const uint64_t whole = ratio_q30 >> 30u;
  const uint64_t fraction = ratio_q30 & ((UINT64_C(1) << 30u) - 1u);

  uint64_t integral = 0u;
  uint64_t addend = frequency;
  uint64_t multiplier = whole;
  while (multiplier != 0u) {
    if ((multiplier & 1u) != 0u) {
      if (integral > UINT64_MAX - addend) {
        note_saturation(stats);
        return UINT64_MAX;
      }
      integral += addend;
    }
    multiplier >>= 1u;
    if (multiplier != 0u) {
      if (addend > UINT64_MAX - addend) {
        note_saturation(stats);
        return UINT64_MAX;
      }
      addend += addend;
    }
  }

  const uint64_t high = frequency >> 30u;
  const uint64_t low = frequency & ((UINT64_C(1) << 30u) - 1u);
  uint64_t fractional = high * fraction;
  const uint64_t low_product = low * fraction;
  fractional += low_product >> 30u;
  if ((low_product & ((UINT64_C(1) << 30u) - 1u)) >=
      (UINT64_C(1) << 29u)) {
    ++fractional;
  }
  if (integral > UINT64_MAX - fractional) {
    note_saturation(stats);
    return UINT64_MAX;
  }
  return integral + fractional;
}

bool src_coefficient_row(uint32_t phase, const int32_t** row,
                         bool* reverse_taps, NumericStats& stats) {
  if (row == 0 || reverse_taps == 0 || phase >= 147u) {
    note_invalid_index(stats);
    if (row != 0) {
      *row = 0;
    }
    if (reverse_taps != 0) {
      *reverse_taps = false;
    }
    return false;
  }
  *reverse_taps = phase >= 74u;
  const uint32_t stored_phase = *reverse_taps ? 147u - phase : phase;
  *row = kSrcCoefficientsQ31[stored_phase];
  return true;
}
"""
    return "\n".join(
        provenance
        + ['#include "tables.hpp"', "", "namespace rings {", "namespace dsp {", ""]
        + definitions
        + [runtime, "}  // namespace dsp", "}  // namespace rings", ""]
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resources", type=Path, default=DEFAULT_RESOURCES)
    parser.add_argument("--units", type=Path, default=DEFAULT_UNITS)
    parser.add_argument("--src-q31", type=Path, default=DEFAULT_SRC_Q31)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    paths = (args.resources, args.units, args.src_q31)
    for path in paths:
        if not path.is_file():
            parser.error(f"required pinned input does not exist: {path}")
    for path in paths + (args.output,):
        resolved = path.resolve()
        if not resolved.is_relative_to(ROOT):
            parser.error(f"input/output must stay inside repository: {path}")

    output = emit_cpp(args.resources.resolve(), args.units.resolve(),
                      args.src_q31.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
