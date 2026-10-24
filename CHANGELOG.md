# Changelog — wSzki fork of open-xy

Newest first. One bullet per landed change; dates are ISO.

## 2026-10-24

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
