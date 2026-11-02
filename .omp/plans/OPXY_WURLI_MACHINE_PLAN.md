# OP-XY Wurli machine — browser-preview working version

## Context

Port a reduced, fixed-point reimplementation of [openwurli](https://github.com/hal0zer0/openwurli) (Wurlitzer 200A physical model, Rust/CLAP) to the open-xy SDK as a synth machine, until it plays in the SDK's browser workbench (`serve_harness.py` preview). Device build, catalog submission and hardware profiling are out of scope; everything written must stay device-legal (integer per-sample DSP, no allocation, C++11, no `<cmath>`), so the same sources compile for the Blackfin later.

The machine is a new repository beside this SDK checkout, created by the SDK scaffold. No files in `/Users/wsz/.tmp/op-xy-wurli` are modified except ignored `local/` preview outputs.

## Model decisions (fixed, do not revisit)

- **Chain per voice**: 7-mode modal reed (sine-LUT oscillators + multiplicative decay) → onset ramp (hammer) → attack-noise burst (bandpass biquad, LCG noise) → electrostatic pickup (per-note 256-entry capacitance-law lookup + one-pole network recursion) → tremolo (amplitude LFO) → speaker stage (one-pole HPF+LPF, blended) → volume (audio-taper pot).
- **Velocity is fixed at 1.0 (ff)** — the SDK render callback receives no velocity. This makes `velocity_scurve(1.0)=1.0`, `vel_blend=1.0`, `dwell` at ff, `onset_ramp_time` at ff.
- **Dropped vs openwurli**: MNA preamp/power-amp solvers, 2x oversampling, MLP corrections, thermal noise, damper-on-note-off (stock envelope owns release), per-note variation kept (cheap, deterministic).
- **All per-sample audio math in Q31** via `opxy::q31::multiply/add/half`. Float is allowed only in note-on/per-block coefficient setup, via hand-written helpers (no `<cmath>` anywhere in the machine).
- **Per-block pitch**: mode increments and the pickup network coefficient are computed once per block from `pitch[0]`; intra-block pitch glide is ignored (1.45 ms, inaudible). This avoids per-sample 64-bit multiplies.
- **Range**: MIDI 33–96 (the 200A's 64 reeds). Notes outside clamp to 33/96.
- **No division in the per-sample loop.** The pickup's `1/(m_avg+beta)` uses a per-block reciprocal `r0` refined per sample by one Newton step `r0*(2 - (m_avg+beta)*r0)`.

## Constants source of truth

Port constants from these files (fetch raw from GitHub `main` if not cloned; read them before translating):
- `crates/openwurli-dsp/src/pickup.rs` — `C_TOTAL=240e-12`, `C2_BASE=220e-12`, `FC_REF_C2_GROUND=880.5` → `PICKUP_FC = 880.5*(240+220)/240 = 1075.8 Hz`, `TAU=1/(2π·PICKUP_FC)`, `V_POLARIZING=147`, `C_REED0=3e-12`, `C_REED_RATIO=C_REED0/C_TOTAL`, `PICKUP_SENSITIVITY=1.8375`, `PICKUP_REST_OFFSET_MM=0.085`, band sign (+1 for midi≤52, −1 above), network recursion `w=(w*(m_avg-beta)-(s-s_prev))/(m_avg+beta)`, `m=1+C_REED_RATIO*s`.
- `crates/openwurli-dsp/src/pickup_tables.rs` — four spline tables (knots `y`, second derivatives `m`, `u0=-4.5`, `du=0.025`, 361 knots each) and the midi→table ranges (`midi-32`: 1–14, 15–20, 21–42, 43–64).
- `crates/openwurli-dsp/src/reed.rs` — structure only (we use LUT oscillators, not quadrature rotation; see approach).
- `crates/openwurli-dsp/src/hammer.rs` — `dwell_time` (0.75 cycles at ff, clamp 0.0003–0.020 s), `dwell_attenuation` (Gaussian σ=8 in f·T units, normalized to mode 0), `onset_ramp_time` (1.0 period at ff, 2 ms floor), attack noise (`amplitude=0.025·v²` = 0.025 at v=1, `tau=0.003`, duration 0.015 s, center `clamp(5·f0, 200, 2000)`, bandpass Q 0.7, LCG `state*1664525+1013904223`, 16-sample fade-in).
- `crates/openwurli-dsp/src/tables.rs` — `NUM_MODES=7`, `BASE_MODE_AMPLITUDES=[1.0,0.005,0.0035,0.0018,0.0011,0.0007,0.0005]`, `midi_to_freq`, `tip_mass_ratio` (5-anchor linear interp), eigenvalue table (8 rows × 7) + `mode_ratios=(beta/beta1)²`, `reed_length_mm`, `reed_blank_dims`, `reed_compliance`, `spatial_coupling_coefficients` (Simpson n=32, normalized to mode 1), `fundamental_decay_rate = max(3.0, 0.005·f^1.22)` dB/s, `mode_decay_rates = base·ratio²` dB/s, `PROVISIONAL_FF_SWING_MM` (24 knots) + `log_interp_by_side` (linear-in-log, never across midi 52|53), `PROVISIONAL_AIR_DRAG_DB_PER_MM` (24 knots) + drag scale 1.0, `velocity_exponent` (bell, center 62, σ 15, max 1.7, treble min 1.3, bass min 0.55), `velocity_scurve` (k=1.5), `register_trim_db` (13 anchors), `output_scale_with_config` (target_db −29.55, voicing_slope −0.04, `pickup_rms_proxy_law` = 64-point DFT over H1–H8 through the one-pole HPF at `PICKUP_FC`), `audio_taper` (15 %-at-center two-slope), `volume_pot_gain` may be ignored — we use plain `audio_taper` gain.
- `crates/openwurli-dsp/src/variation.rs` — `hash_f64` (FNV-1a chain), `freq_detune` (±0.173 %, seed 0xDEAD), `mode_amplitude_offsets` (±8 %, seeds 0xBEEF+i).

Port formulas as written; fixed-point conversions happen at the boundary (see step 3).

## Steps

### 1. Scaffold

From `/Users/wsz/.tmp/op-xy-wurli`:

1. `python3 -c "import numpy"` — if it fails, create `.venv` (`python3 -m venv .venv && .venv/bin/pip install numpy==2.3.5`) and use `.venv/bin/python` in later commands.
2. `python3 sdk.py doctor --preview` — must print `PASS: WebAssembly compilation, linking and execution`. On macOS add `--cxx "$(brew --prefix llvm)/bin/clang++"` (Xcode clang lacks wasm-ld) and reuse that `--cxx` on every `preview`/`test` call.
3. `python3 sdk.py new --repository --id wurli --name "Wurli" --out ../wurli`
   - id `wurli` is valid (≤15 chars, no catalog collision) and becomes the C++ namespace.
   - 4 controls (default), so the generated panel artwork (two dial nodes) works unchanged.
   - Generates `../wurli/{definition.json, core.h, engine.cpp, display.h, display.cpp, panel.svg.fragment, machine.json, tests/sound.cpp, README.md, AGENTS.md, LICENSE, .gitignore, .github/workflows/machine.yml}`.
4. `git -C ../wurli init && git -C ../wurli add -A && git -C ../wurli commit -m "chore: scaffold wurli machine"` (scaffold baseline; later steps commit on top).

### 2. Generate `pickup_tables.h`

Write `/tmp/gen_pickup_tables.py` that fetches `pickup_tables.rs` (raw URL) and emits `../wurli/pickup_tables.h`:

- For each of `R01_14, R15_20, R21_42, R43_64`: `static const float <NAME>_Y[361]`, `static const float <NAME>_M[361]` (copy every literal), and a struct entry `{float u0; float du; const float* y; const float* m;}`.
- Helpers: `const PickupLawTable* pickup_law_for(unsigned midi)` (ranges `midi-32` 1–14 / 15–20 / 21–42 / 43–64) and `int pickup_band_sign(unsigned midi)` (+1 for `midi-32 ≤ 20`, else −1) — port from `pickup.rs`.
- Script self-check: assert each parsed array has 361 entries and first/last values match the Rust file exactly; print the four table names on success.
- Include from `engine.cpp` only.

### 3. `engine_math.h` (no `<cmath>`, deterministic, C++11)

Namespace `wurli`. All functions free of allocation and statics:

- `float exp_neg(float x)` — `exp(-x)` for x ≥ 0: `exp2f_impl(-x * 1.4426950408889634f)`; `exp2f_impl(y)` = `ldexpf(poly2fract(y))` with the fraction via degree-5 minimax polynomial on [0,1) and the integer part by bit construction (`union { float f; unsigned u; }` mantissa/exponent assembly). Relative error target ≤ 1e-6.
- `float log2_pos(float x)` — x > 0: normalize via exponent bits, degree-5 poly on mantissa [1,2). Used for midi detection and `10^(db/20)`.
- `float pow10_db(float db)` — `exp_neg_or_pos`: implement `exp_pos` too (same helper with sign), then `pow10_db(db) = exp2f_impl(db * 3.3219280948873623f)` handling both signs.
- `float f_pow(float f, float p)` — `exp2f_impl(p * log2_pos(f))` (for `f^1.22`).
- `void sincos_poly(float x, float* s, float* c)` — argument reduction to [-π,π] (no large args here), degree-7 minimax sin on [-π/2, π/2] + quadrant folding for both outputs. Used only at note-on for mode rotation-free needs: `dwell` needs none, but sine-table fill and biquad center need sin/cos; CORDIC unnecessary — polynomial is fine.
- `float cosh_poly(float x)`, `float sinh_poly(float x)` — via `exp_pos`/`exp_neg` (for `mode_shape` inside `spatial_coupling_coefficients`; x ≤ ~20.4, float range safe).
- `static const float PI_F = 3.14159265358979f;`
- Self-test in `tests/sound.cpp` (step 7): compare `exp_neg`, `log2_pos`, `sincos_poly` against reference constants at a handful of points (hard-coded expected values, tolerance 1e-4 relative).

### 4. `core.h` — state layout

```cpp
#pragma once
#include <opxy/audio.hpp>

namespace wurli {
struct Shared {
    opxy::u32 valid;                 // 0 until lazily filled
    opxy::i32 sine[1024];            // full-cycle sine, Q31, filled on first initialize
    opxy::i32 ratios[64][7];         // Q31 mode frequency ratios
    opxy::i32 amps[64][7];           // Q31: BASE_MODE_AMPLITUDES x dwell x variation x coupling
    opxy::i32 decay_mult[64][7];     // Q31 per-sample multiplicative decay (natural decay only)
    opxy::i32 out_scale[64];         // Q31 post-pickup voicing gain
    opxy::i32 swing_q31[64];         // Q31 swing in mm (sign folded: multiply by band sign at use)
    opxy::u8  law_index[64];         // 0..3
    opxy::i8  band_sign[64];         // +1 / -1
    opxy::i32 noise_b[64][5];        // Q31 RBJ bandpass b0,b1,b2,a1,a2 (a1,a2 negated for sum form)
    opxy::i32 noise_amp[64];         // Q31 initial burst amplitude
    opxy::i32 noise_decay[64];       // Q31 per-sample multiplier
};
struct Voice {
    opxy::u32 mode_phase[7];
    opxy::i32 mode_env[7];
    opxy::u32 onset_count;           // samples since note start (ramp window)
    opxy::i32 onset_s, onset_c;      // quadrature pair for the raised-cosine ramp
    opxy::i32 pickup_s_prev, pickup_m_prev, pickup_w;
    opxy::i32 noise_x1, noise_x2, noise_y1, noise_y2;
    opxy::i32 noise_env;             // current burst amplitude incl. decay
    opxy::u32 noise_remaining;       // samples left (includes fade-in)
    opxy::u32 rng;
    opxy::u32 trem_phase;
    opxy::i32 trem_lpf;              // reserved speaker/trem smoothing state
    opxy::i32 speaker_hpf, speaker_lpf;
    opxy::i32 law[256];              // per-note law lookup over this note's swing
    opxy::i32 law_lo, law_hi;        // Q31 u-range endpoints the 256 entries span
    opxy::u8  midi;
    opxy::u8  active;                // 1 after start()
};
}
```

Declare in `definition.json` `resources`: `voiceBytes`/`voiceAlignment` = 4-aligned `sizeof(Voice)` (~1.1 KB — confirm from the compiler's size check output on first `sdk.py test` run and set the exact number), `voiceCapacity: 8`, `sharedBytes` = `sizeof(Shared)` (~12 KB, same confirmation), `outputPeakQ31: 1073741824`, `display: {stateBytes: 4, stateAlignment: 4, paths: 2, points: 64, nodes: 2, texts: 0, palette: 16}` (unchanged from scaffold).

Q31 scale conventions: physical quantities in Q31 (1.0 = 0x7fffffff); `u` in mm scaled by 2^20 per mm (mm_q20) inside the law lookup to keep headroom (swing ≤ ~3.9 mm); document each scale at its declaration site.

### 5. `engine.cpp` — DSP implementation

Callbacks in namespace `wurli`, per the SDK contract (`initialize/start/destroy/render`, serial per voice). Shared cache fill happens inside `initialize`/`start` when `shared.valid == 0` (first voice pays ~50k setup ops once per process; per-note work after that is small). Include `core.h`, `engine_math.h`, `pickup_tables.h`, `<opxy/q31.hpp>`.

`start(voice, shared, owner)` — note-on (owner's low byte + block-free constants give note identity via pitch, see render):

1. Derive midi from `pitch[0]` — **render needs pitch, but start doesn't receive it**, so do note-on inside `render` on the first block: set `voice.active = 1` in `start`, and in `render`, if `voice.note_midi_pending` (a `u8` flag in Voice; add it), run note-on using `pitch[0]`. (`initialize`/`start` only zero state.)
2. Note-on, given midi m ∈ [33, 96] clamped:
   - Fill Shared caches if `!valid` (sine table first: 1024 Q31 points over 2π via `sincos_poly`; then the per-note tables for **all 64 notes**, ~ms of setup, once):
     - `ratios`: eigenvalue interpolation (8-row table, linear in mu) → `(beta_n/beta_1)²` as Q31.
     - `amps`: `BASE_MODE_AMPLITUDES[i] · dwell_attenuation(ff)[i] · variation::mode_amplitude_offsets[i] · spatial_coupling[i]` (Simpson n=32, normalized to κ₁) → Q31.
     - `decay_mult[i] = exp(-decay_db[i]/8.686/44100)` → Q31 (`decay_db = max(3.0, 0.005·f^1.22)·ratio_i²`).
     - `out_scale[m] = output_scale_with_config(m, 1.0)` ported exactly (uses `pickup_rms_proxy_law`: 64-point DFT, H1–H8, HPF at PICKUP_FC; reference note 60; `register_trim_db` anchors; vel_blend=1) → Q31.
     - `swing_q31[m] = max(0.02, log_interp_by_side(swing knots, Central pick))` → Q31-mm (2^20/mm scale, see below).
     - `law_index`, `band_sign` from `pickup_tables.h` helpers.
     - `noise_b`: RBJ bandpass at `center = clamp(5·f0, 200, 2000)` Hz, Q 0.7, fs 44100 — coefficients in float at cache time, stored Q31 with `a1,a2` pre-negated so the difference equation is a plain 5-term sum of `q31::multiply`s.
     - `noise_amp = 0.025` Q31; `noise_decay = exp(-1/(0.003·44100))` Q31.
   - Per-voice note-on: copy midi/law fields; generate `law[256]`: 256 spline evaluations of `law.eval(u)` over `u ∈ [−0.085−swing, −0.085+swing]` mm (spline eval ported from `PickupLawTable::eval`, including the C2 cubic terms and end clamping) minus `c_rest = eval(−0.085)`; store `law_lo/law_hi` in mm_q20; zero `mode_phase`, `mode_env = 0x7fffffff` each; `pickup_* = 0`; noise biquad state zero; `noise_env = noise_amp[m]`, `noise_remaining = round(0.015·44100)` = 662, `rng = m·2654435761` (wrapping u32); `onset_count = 0`; `trem_phase` = 0; speaker states zero; `active = 1`.
3. `destroy`: `active = 0` only.

`render(voice, shared, owner, output, parameters, pitch)` — per block:

- If `!voice.active`: write 64 zeros, return.
- If `voice.midi_pending` (Voice gains `u8 midi_pending`, set 1 in `start`): run note-on from `pitch[0]` (step 2 above), clear the flag. If `pitch[0] == 0` on the first block, render silence and keep waiting (defensive; stock always supplies pitch).
- Block setup (float allowed here):
  - `f0 = pitch[0]·44100/2^32` (double), `midi = clamp(round(69 + 12·log2(f0/440)), 33, 96)`, `voice.midi = midi`. Cache index `i = midi − 33`.
  - Per-mode increment `inc_k = (u64(pitch[0]) · u64(ratios[i][k] as u32)) >> 31` (wrapping modulo 2^32; ratios are positive ≤ ~141² in Q31 → product ≤ 2^63, use `u64` — 64-bit only here, 7 times per block).
  - `beta_q31` (constant network pole: `π·PICKUP_FC/44100` → Q31) and per-block reciprocal seed `r0 = reciprocal_q31(0x7fffffff + beta_q31)` via `f32` division at block start.
  - Tremolo: depth `= parameters[1]`, rate Hz `= 1 + 9·parameters[2]`, `trem_inc = round(rate·2^32/44100)`; speaker character `p = parameters[3]`; speaker poles fixed: HPF ~150 Hz, LPF ~4500 Hz one-pole coefficients (Q31, constants).
  - Volume gain `= audio_taper(parameters[0])` ported exactly → Q31.
- Sample loop, i = 0..63, all Q31:
  - **Onset ramp**: while `onset_count < onset_len` (per-note: `max(0.002, 1/f0)·44100` samples, computed at note-on into a Voice field `onset_len u32` — add it): rotate the quadrature pair by the per-note increment (`onset_s/c`), `onset = half(0x7fffffff - onset_c)`; after the ramp, `onset = 0x7fffffff`.
  - **Reed sum** `d = 0`: for k in 0..6: `v = sine_lut(mode_phase[k])` (14-bit index + 14-bit fractional linear interp into `shared.sine`, fold to [0,1024) with quadrant negation); `d += q31::multiply(q31::multiply(v, voice.mode_env[k]), shared.amps[i][k])`; `mode_env[k] = q31::multiply(mode_env[k], shared.decay_mult[i][k])`; `mode_phase[k] += inc_k`.
  - **Attack noise** (while `noise_remaining`): fade-in over first 16 samples via `t²` quadratic (`(pos·pos)>>4` scaled); LCG step; 5-term biquad with `q31::multiply`s; `noise_env = q31::multiply(noise_env, shared.noise_decay[i])`; `d += burst`.
  - **Pickup** (port `Pickup::process` exactly): `u = q31::multiply(d, band_sign·swing_q31) − REST_MM_Q20` — implement `d` (Q31) → mm_q20 via `multiply(d, swing_q31_mm20)`; `s = law_lookup(u)` (map u into [law_lo, law_hi] → 256-entry linear interp; clamp outside); `m = 0x7fffffff + q31::multiply(C_REED_RATIO_Q31, s)`; `m_avg = q31::half(m + pickup_m_prev)`; `ds = s - pickup_s_prev`; `r = r0·(2 − (m_avg+beta)·r0)` (two Q31 multiplies + add, `r0` carried per block in a Voice field); `pickup_w = q31::multiply(pickup_w, m_avg − beta) − ds` then `q31::multiply(that, r)`; update `s_prev/m_prev`; `x = q31::multiply(pickup_w, PICKUP_SENSITIVITY_Q31)`.
  - **Tremolo**: `trem_phase += trem_inc`; `g = 0x7fffffff − q31::multiply(depth, q31::half(0x7fffffff + sine_lut(trem_phase)))` (range [1−depth, 1]); `x = q31::multiply(x, g)`.
  - **Speaker**: HPF `y = x − speaker_hpf; speaker_hpf = x − q31::multiply(hpf_c, y)`; LPF `speaker_lpf += q31::multiply(lpf_c, y − speaker_lpf)`; `x = x + q31::multiply(p, speaker_lpf − x)`.
  - **Volume + voicing**: `x = q31::multiply(x, shared.out_scale[i])`; `output[i] = q31::multiply(x, vol_gain)`.
- Per-block per-note increments and `r0` go in Voice fields added for them (`u32 mode_inc[7]`, `i32 net_r0`) — compute once, loop reads fields.

No `#if`, no platform macros, no inline asm (the `__bfin__` branches live only inside `opxy/q31.hpp`), no includes beyond machine headers + `opxy/*.hpp`.

### 6. Display + artwork + definition

- `display.cpp`: keep the scaffold's `Frame<2, 64, 2, 0, 16>` and structure; replace the starter's two rotating squares with two horizontal reed bars (long/short, x-centered) whose vertical wobble amplitude follows `input.parameters[1]` (trem depth). Keep `reduced_motion` handling (jump to rest). No other display work.
- `definition.json`:
  ```json
  "parameters": [
    {"id":"volume","label":"Volume","default":0.65,"mapping":{"kind":"linear","minimum":0,"maximum":1},"display":"percent"},
    {"id":"tremdepth","label":"Trem Depth","default":0.5,"mapping":{"kind":"linear","minimum":0,"maximum":1},"display":"percent"},
    {"id":"tremrate","label":"Trem Rate","default":0.5111,"mapping":{"kind":"linear","minimum":0,"maximum":1},"display":"percent"},
    {"id":"speaker","label":"Speaker","default":0,"mapping":{"kind":"linear","minimum":0,"maximum":1},"display":"percent"}
  ],
  "presets": {
    "Dry":     {"volume":0.65,"tremdepth":0,"tremrate":0.5111,"speaker":0},
    "Tremolo": {"volume":0.65,"tremdepth":0.5,"tremrate":0.5111,"speaker":0.3},
    "Cabinet": {"volume":0.8,"tremdepth":0.4,"tremrate":0.5111,"speaker":1}
  }
  ```
  Labels ≤ 8 chars, clockwise-up, color controls default off — conventions satisfied. Keep the `resources` block per step 4.
- `README.md`: replace scaffold text with a short description (what it is, the four controls, that it is a reduced fixed-point port of openwurli, velocity fixed at ff). Do not touch `AGENTS.md`/`LICENSE`/CI scaffold.

### 7. `tests/sound.cpp` — machine sound checks

Extend the generated test (keep `#include "../engine.cpp"` pattern, host-compiled). Cases:

1. **Sounding + bounded**: A4 pitch `42852281u`, defaults, 690 blocks — some nonzero output, every sample within ±`1073741824`.
2. **Registers**: pitch increments for 55 Hz (`5368709u`), 262.63 Hz (`~25.5M`, compute `round(f·2^32/44100)` in the test), 2093 Hz — each produces nonzero output within a second.
3. **Decay**: note 60-equivalent pitch; RMS over blocks 0–20 > 3× RMS over blocks 620–690 (natural decay works; stock envelope is not modeled).
4. **Determinism**: two fresh voices, same pitch/params → identical 690-block output arrays (deterministic RNG).
5. **Tremolo modulation**: depth 0.9 vs depth 0 — per-block peak variance across blocks 200–400 is > 10× larger with depth 0.9.
6. **Peak calibration**: defaults, note 60, full volume `parameters[0] = 0x7fffffff` — peak in [0.2, 0.6] × 1073741824. This is the acceptance gate for `OUTPUT_NORM` (below).
7. **Math helpers**: `exp_neg(0) = 1`, `exp_neg(1) ≈ 0.3679`, `log2_pos(4) ≈ 2`, `sincos_poly(π/2)` sin ≈ 1 — tolerances 1e-3 absolute (float pipeline).

Normalization: after the first full build, run the peak calibration test; set a single constant `OUTPUT_NORM_Q31` in `engine.cpp` (applied right before the volume multiply, i.e. folded into `out_scale` at cache time — one place) so the measured peak lands mid-band; record the measured basis in a one-line comment. Re-run until the gate holds.

### 8. Iterate to green

- `python3 sdk.py test --machine ../wurli [--cxx ...]` — fixes records/sources, builds the preview, runs `tests/*.cpp` on the host, prints the CPU estimate.
- Gate: all sound checks pass **and** the printed estimate ≤ **15,000** Blackfin instructions per voice block (hard fail above 24,000). Fix the declared `voiceBytes`/`sharedBytes` to the compiler-reported sizes when the size check complains.
- Contingency ladder if the estimate exceeds 15,000 (apply in order, re-measure after each):
  1. Sine LUT nearest-neighbor (drop interpolation) — costs a little aliasing at high partials, mode 7 sits ~40 dB down by then.
  2. Law lookup: 128 entries instead of 256.
  3. Modes 7 → 5 (drop the two weakest: amplitudes 0.0007/0.0005; documented fidelity loss).
- Commit after each green state: `git -C ../wurli add -A && git -C ../wurli commit -m "feat: ..."`.

### 9. Browser proof

1. `python3 sdk.py preview --source ../wurli --out local/wurli-preview-001 [--cxx ...]` (fresh output dir each build; `--overwrite` to rebuild into the same one).
2. `python3 tools/serve_harness.py --preview local/wurli-preview-001` → http://127.0.0.1:8917/.
3. Headless verification via the browser tool (`browser.open` + `tab.run`):
   - Page renders: machine title "Wurli", 4 controls, panel artwork present, no console errors.
   - Press keys (`A W S E D F T G Y H U J K` mapped in the workbench) via dispatched keydown events; workbench reports no audio errors.
   - Switch to the "Tremolo" preset; confirm parameter values change in the UI.
   - DSP behavior itself is proven by the host sound tests (step 7); the browser check proves wiring (audio starts, voices trigger, no runtime errors). State plainly that audibility/timbre needs human ears; the user does the listening pass.

## Critical files

| File | Why |
|---|---|
| `../wurli/engine.cpp` | All DSP; the note-on cache fill and the Q31 sample loop are the load-bearing work |
| `../wurli/core.h` | `Voice`/`Shared` layout — sizes feed `definition.json`; keep fields exactly as listed |
| `../wurli/pickup_tables.h` | Generated spline data + law/band helpers; the pickup is the whole harmonic character |
| `../wurli/definition.json` | Parameters, presets, resources; size declarations must match compiled types |
| `../wurli/tests/sound.cpp` | Acceptance: decay, determinism, registers, tremolo, peak calibration, math helpers |
| SDK contract: `docs/api.md`, `docs/ux.md`, `sdk/templates/synth/*` | Callback signatures, Q31 semantics, resource fields, UX conventions — reread before deviating |

## Assumptions & contingencies

- **`python3` + numpy + LLVM wasm toolchain available** (doctor gates this). If `brew` llvm is missing, install it (`brew install llvm`) — user-visible prerequisite, flagged before proceeding.
- **Scaffold command shape** (`sdk.py new --repository --id ... --out ../wurli`) is from `docs/first-machine.md` (read this session). If the flag set differs in this checkout's VERSION, follow `python3 sdk.py new --help` output — the scaffolded file set is what matters.
- **CPU estimate ≤ 15,000 is expected but not guaranteed**; the contingency ladder in step 8 is pre-approved, apply top-down without re-asking.
- **Pitch-to-midi identity uses `pitch[0]` per block**; pitch-bend within a block is ignored by design. If the preview reveals audible stepping during bends, revisit only after everything else is green.
- If the browser workbench cannot be driven headlessly (harness changes), fall back to asking the user to open http://127.0.0.1:8917/ and report — DSP proof stays the host test suite.