# Changelog — wSzki fork of open-xy

Newest first. One bullet per landed change; dates are ISO.

## 2026-10-25

- Increased the three Rings string strokes and five Damping swatches to 0.8
  path units; lightened the Damping palette from white `#F4F6F6` to `#899393`.
  The initial string stroke remains white. No DSP, geometry, or timing changes.

- Corrected Rings Brightness to change string length at a constant thin
  stroke, reduced Damping's palette to five shades, and shortened the Model
  readout to `sympat` to fit the bar. Faster redraws are Rings-only; the
  note-event visualization is not PCM-synchronized. Sound DSP is unchanged.

- Refreshed the Rings LCD UI to three note-reactive strings drawn via the
  shared C++ `Frame<3,32,3,1>` API, with Model/Output alternation in the top
  row; sound engine unchanged.
- Tuned the Rings strings after audition feedback: steep single-stroke
  diagonals (no doubled blurry edge), visible ring-out shortened to stay inside
  the audible decay, and the browser top row restyled as the stock stepped
  header. DisplayState shrank to 40 B.
- Remapped the Rings strings to the controls per audition feedback: horizontal
  at Structure 0 with Structure tilt, Brightness stroke width, Damping shade
  (white → dark grey via a 15-entry palette), Position spacing; vibration
  speeded to 12–30 Hz and ring-out cut to the preview's audible window
  (~0.2–0.65 s). State is 44 B with the slope cache restored.
- Raised only the visible Rings monitor gain (default 2.6, max 4, about
  +12 dB over the old 0.65); DSP and output routing unchanged. Evidence in
  `local/rings-night-001/` and the worklog.

## 2026-10-24

- Added Make shortcuts for serving an existing browser preview, explicitly
  building a fresh WASM preview, running SDK checks and listing overrides.

- Added explicit input-only Web MIDI keyboard playback with a device picker, note/velocity handling and disconnect cleanup; typing and on-screen playback remain available.
- Clarified Rings model-bank wording and recorded the final host/WASM, callback-audio and playable-preview verification evidence; native/release qualification remains deferred.
- Corrected overflowing Rings cutoff constants, restoring high-frequency string energy and the long-decay filter transition without gain compensation.
- Made provisioning and maintaining `WORKLOG.md` and `CHANGELOG.md` an explicit fork convention.
- Fixed lost fractional feedback in the Rings exciter DC blocker, removing an unintended string-model DC tail; updated the measured shared-resource declaration.
- Added opt-in collection selection and native compile proof; defaults and profile/ABI remain unchanged. Collection path-escape coverage no longer depends on temporary directories being outside the checkout.
- Updated the Rings implementation plan and fork instructions to target close musical behavior rather than exact floating-point identity, while retaining the three-model scope and runtime safety requirements.
- Added engine-local Rings fixed-point arithmetic, pinned-source table generation and shared SRC constants, with exercised host numeric/sanitizer checks.
- Added an opt-in three-model Rings engine with two leased contexts, six controls, presets, display and browser preview. Host/runtime checks are exercised; reference-informed tonal refinement and native qualification remain separate.
- Corrected the integer SVF state update to round raw integrator accumulators once, with a nonzero-state regression and sanitizer exercise.

- Independently audited all 115 research references and the SDK/MI planning
  claims. The previous research is not execution-ready without the corrections
  recorded in
  [the dated audit](local/research/outputs/opxy-hardware-mi-research-audit-2026-10-24.md).
  Current successors:
  [research brief](local/research/outputs/opxy-hardware-mi-research-2026-10-24.md)
  and [MI port plan](local/port-plan-2026-10-24.md).
  Preserved the closed originals; restored original Tides/Tides2 eligibility,
  corrected memory accounting and timing/oracle/recovery claims, and recovered
  the FCC photo evidence. Runtime source and device state unchanged.

## 2026-10-23

- Added fork router `AGENTS_WSZ.md`; appended a pointer section to `AGENTS.md`
  (the only fork edit to an upstream file).
- Added `CHANGELOG.md` (this file) and `WORKLOG.md`; both indexed from
  `AGENTS_WSZ.md`.
- Port planning: machine-ABI constraints digest (BF707 no-FPU, 508 KiB shared
  module region, effect State ≤ 476 B, mono Q31 synth voices, ≤8 controls),
  candidate engine tiers with license sweep, and the Mutable Instruments focus.
  Saved to `local/port-plan.md` (ignored tree, not committed).
- Corrected the MI effort model after upstream verification: Plaits and Rings
  are float DSP (Braids was the only int16/int32 MI module); MI ports are
  float→Q31 rewrite campaigns. Reflected in `local/port-plan.md` §3.
- Started the deep-research corpus at `local/research/` (OP-XY hardware,
  Blackfin+/BF707 architecture, MI code audit, embedded port precedents,
  brick-risk & recovery). Plan:
  `local/research/outputs/.plans/opxy-hardware-mi-research.md`.

- Deep-research corpus delivered at `local/research/outputs/`
  (`opxy-hardware-mi-research.md` + provenance, 5 research notes, plan and
  per-researcher briefs; 114 sources, verification PASS WITH NOTES). Port
  plan updated accordingly: stmlib Q31 shim precedes the Peaks port; Edges
  excluded (GPL-3.0 + ATxmega asm); Tides 2 reclassified as modulation engine;
  Rings/Elements RAM budgets made concrete; te-boot recovery and
  bootloader-security churn recorded.
