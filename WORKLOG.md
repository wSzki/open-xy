# Worklog — wSzki fork of open-xy

One entry per session, newest first. Findings and data live in dated files
under `local/research/` or in `local/port-plan.md`; this file only records
what happened and where the output landed.

## 2026-10-23

- Planned open-source engine ports for the OP-XY SDK: digested the machine ABI
  from `docs/api.md` / `docs/abi-1.2.0.md` / `docs/build.md` /
  `docs/toolchain.md` (kinds, budgets, guards), ran a license sweep over ~30
  candidate engines, and audited the existing `machines/` ports. Output:
  `local/port-plan.md`.
- Accepted an external review correction and verified it against upstream
  sources: `plaits/dsp/engine/virtual_analog_engine.cc` / `fm_engine.cc` and
  `rings/dsp/resonator.cc` / `string.h` (`DelayLine<float, 2048>`) are float
  DSP. Consequence recorded in the plan: every remaining MI port is a
  float→Q31 rewrite campaign (timestretch's Capicola adaptation is the
  in-repo precedent); Braids was ported first precisely because it is the only
  int16/int32 MI module.
- Restructured fork routing: minimal pointer section appended to `AGENTS.md`
  → `AGENTS_WSZ.md` (fork rules + pointers) → `CHANGELOG.md`, `WORKLOG.md`,
  `local/port-plan.md`, `local/research/`. Base repo otherwise untouched;
  research and generated outputs stay in ignored `local/`.
- Deep research kicked off (`opxy-hardware-mi-research`): plan written to
  `local/research/outputs/.plans/opxy-hardware-mi-research.md`; awaiting user
  confirmation before dispatching researchers. Brick-risk framing: the dev
  loop is device-free (WASM preview, `sdk.py check`, host build verification);
  risk begins only at install/flash, which happens solely on explicit user
  instruction.
- Open threads: Peaks upstream location + license audit (both probed paths
  404'd); researcher dispatch T1–T4 after plan confirmation; FCC
  internal-photo exhibit 7769481 extraction (PDF parsing blocked by protocol —
  cite from metadata).

- Deep research `opxy-hardware-mi-research` executed end-to-end: 4 researcher
  subagents + local evidence pack → lead synthesis → citation verifier (114
  consolidated sources) → reviewer (no FATAL; 3 MAJOR + 6 MINOR) → revision
  (all MAJOR fixed, diff-verified on disk) → delivery. Final:
  `local/research/outputs/opxy-hardware-mi-research.md` + `.provenance.md`
  (PASS WITH NOTES). Key results: toolchain provenance pinned
  (deanm1278/blackfin-plus-gnu, classic-Blackfin codegen, soft-float
  240-950 cycles/op via generic libgcc); MI corpus audit (only Braids + Peaks
  integer; per-module sample rates and RAM; Edges excluded GPL+ATxmega; stmlib
  Q31 shim identified as the shared chokepoint); no third-party float→fixed
  MI rewrite found in the sweep; te boot = documented recovery path;
  bootloader-security churn = standing watch item. `local/port-plan.md`
  updated with the implications (sequencing now: stmlib shim → Peaks →
  Plaits/FM → Rings → Elements).

- Correction (external review, verified vs upstream): Edges is ATxmega32A4
  (8-bit AVR XMEGA via avrlibx `MCU = atxmega$(MCU_NAME)`), not AVR32 — the
  earlier reading misread `MCU_NAME = 32a4`. Architecture claims fixed across
  the research corpus, port plan and logs; the GPL-3.0 exclusion stands on
  license. Aleph's AVR32 (different device) unaffected.
