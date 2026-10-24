# Worklog — wSzki fork of open-xy

One entry per session, newest first. Findings and data live in dated files
under `local/research/` or in `local/port-plan.md`; this file only records
what happened and where the output landed.

## 2026-10-24

- Completed five independent verification slices covering SDK contracts,
  MI DSP/memory, hardware/recovery, compiler/precedents and all 115 numbered
  references. Lead adjudication and calculations are in
  [the audit](local/research/outputs/opxy-hardware-mi-research-audit-2026-10-24.md);
  detailed evidence and the source ledger are alongside it under `.drafts/`.
- Use [the corrected brief](local/research/outputs/opxy-hardware-mi-research-2026-10-24.md)
  and [the dated MI plan](local/port-plan-2026-10-24.md), not the closed
  2026-10-23 versions, for current decisions. Peaks need not wait for a float
  shim; original Tides is another integer/audio candidate; Tides2 is audio-capable.
  Elements' identified resident storage is already 165,280 B before other
  state, invalidating the old approximately 100-KiB full-module budget.
- Recovered and parsed all seven pages of FCC exhibit 7769481 and inspected
  the board images; no confident exact chip identification was possible.
  Recovered the manufacturer BF706/BF707 memory-map page through a mirror:
  current module linkage is in its external-DDR range, not L2 SRAM.
- Implemented opt-in collection selection across `tooling/{machines,native,browser_kit,checks}.py` with regression tests and `docs/build.md` guidance. Defaults, profile/ABI, hashes and reservations remain unchanged. Standalone omitted/default compiles produced identical module SHA-256 (`c7c152c3…d8c0e313`); selected FM2-op-only compile contained no other machine objects (27,900 B allocated span; 36 B audio stack). Outputs: `~/Library/Application Support/OPXY-SDK/private/collection-selection-final-20261024`.
- External Rings float gate stopped registration before integer implementation/engine registration/preview: one prescribed case of 2,286, not the full matrix or full engine, measured 29.033357-cent pitch error against the frozen 10-cent limit (integrated band error −1.06681 dB passed). Independent project and results: `~/Library/Application Support/OPXY-SDK/rings-validation/rings-first-engine` and `~/Library/Application Support/OPXY-SDK/rings-validation/results/rings-004-preflight`; mismatch report: `~/Library/Application Support/OPXY-SDK/rings-validation/results/rings-004-preflight/rate-adaptation-mismatch.md`. Rings is not admitted; `RINGS_ENGINE_PLAN.md` remains frozen unchanged. No device actions, listening, or cycle measurements.
- Final public check (`OPXY_TEST_COLLECTION=does-not-exist.json python3 sdk.py check`): 78 Node tests passed; each normal and `-O` Python run had 120 tests, 114 successful and six private-fixture skips. Native package/kit qualification remains blocked by absent stock inputs and the required Windows x86_64 release host.
- Approved follow-on design keeps the original 48-kHz Rings core and uses a bounded 147/160 streaming SRC. At the user's overnight boundary, paused external-writing work and deferred installs, destructive operations and intentional writes outside this checkout. New host work is sandbox-contained under `local/rings-night-001/`; existing external sources/results remain read-only.
- Completed and independently reviewed the C++11 host-only Q31 SRC proof: 13 cases, 53,249 exact-oracle outputs, all 147 phases, matching normal/ASan+UBSan output and zero observed kernel allocations. Coefficients occupy 75,776 B and host SRC state 2,064 B. Report: `local/rings-night-001/results/native-host-acceptance-cpp11/native_src_host_acceptance.json`. This is not a full Rings fidelity, BF707 cost/helper/stack, module-fit or real-time qualification; the full streaming sound matrix is still pending, and target-native/package outputs remain deferred.
- Re-established complete public checks inside the repository-only write boundary: 78 Node tests passed, and both normal/`-O` Python runs had 120 tests with 114 successful and six private-fixture skips. Fixed the collection symlink-escape fixture to use an isolated guarded root even when `TMPDIR` is inside the checkout; production path guards are unchanged. Repo-local compiler selection restored all eight host C++ checks. `local/rings-night-001/public-check.sb` permits loopback fixture servers and nested temporary Git repositories while protecting the real `.git`, toolchain and preview environment; no target-native/package qualification was inferred.
- The first 2,286-case streaming attempt stopped at 1,744 cases after its one-hour tool deadline, with three reported failures and no completed matrix summary. Same-render diagnosis and original-Part coefficient readback showed that the old pitch oracle counted multiple FFT maxima around one bank mode as separate modal partials; clipping 1–4 early samples changed those candidate pools. The large reported cents values are not evidence of physical resonator detuning. Source-identified, output-routed modal measurement and independent calibration are being corrected without changing DSP, gain, output limits or sound scope; Rings remains unadmitted.
- Preserved the coefficient tensor and its original producer binding (`e31cf404…`, Python 3.15 / NumPy 2.5.4); mutable analyzer/consumer provenance is recorded separately. Original source snapshots and superseded results remain retained.
- Boundary incident: a worker moved its own temporary baseline-script copy into repository-local trash despite the no-cleanup instruction. Restored the original path by copying the retained bytes, leaving trash and frozen originals untouched; both copies verify as `e31cf404…`. No user data, original evidence, OMP history or outside-repository project files were removed. Further cleanup remains deferred.
- The next full-catalog attempt stopped before audio at a control-wire mismatch, not a measured DSP failure. A Latin-hypercube value rounded to adjacent float32 words when formatted from float64 with nine digits. Corrected every audio/probe sender to round to float32 before serialization and checked actual C++ setter words exactly. Repository-local preflight covered all 762 modal cases: 664 unique banks per role and 762 raw probe rows per role passed under Python `-O`; stale-word corruption is rejected. Evidence: `local/rings-night-001/results/modal-wire-prefetch-20261024-01/prefetch_audit.json`. Full streaming admission remains pending independent review and a fresh complete matrix.
- Completed the frozen streaming matrix at `local/rings-night-001/results/src-full-routed-20261024-02`: 2,286 unique catalog cases, 762 per model; 2,285 pass and one fails (`corner-modal-04-n96`, Mix, mode 43 unidentified). Reference peak 13,421.652315 Hz is 0.998891 Hz from its stored-bank center; the matching candidate 13,421.572736 Hz falls outside the analyzer's 1-Hz identity admission. This is not a passing lane; source diagnosis and independent review are pending, and integer implementation remains gated.
- A read-only containment audit found that SRT 0.0.49 overrides `TMPDIR` to `/tmp/claude` and implicitly allows writes there even with a repository-only `allowWrite`. The full-run compiler metadata records that actual environment, so the earlier strict-containment assumption was invalid. No actual outside-file write has yet been established. Stopped further SRT launches; future probes use the already exercised raw macOS Seatbelt profile with repository-local environment/cache paths. No outside cleanup is authorized or performed.
- Replayed only the failing case with the frozen binaries to retain genuine raw/bounded float64 buffers. Production analysis reproduces the original failure; the raw stream matches the unclipped offline Mix reference within `1.8515677768782268e-8`. A later actual observation uniquely identifies mode 43, but the current loudest-before-identification merge discards it. This diagnoses information loss, not a passing fidelity gate. Evidence: `local/rings-night-001/results/sole-modal-failure-20261024-01/raw-replay/`.
- The offside temporal-ID experiment was not promoted. Its final `temporal-modal-experiment-20261024-01/run06/` run exited 1: 299/326 checks pass; seven of nine required high-Q output configurations remain unresolved under the fixed collision/ownership contract. The historical MIX mode-3 direct-44.1 negative is correctly retained at −29.033458 cents; all three original separated routed-20-ID fixtures pass. A separate actual-production, controlled peak-pool self-comparison demonstrates that retaining the old REF selector while changing only GOT identification can reject identical observations. It is a pool-level diagnostic, not a waveform proof. The canonical analyzer, frozen full02 result and sound thresholds remain unchanged; integer work is stopped pending an explicit consistent modal-identity/reference-validity decision.
- Reviewed upstream [PR #58](https://github.com/etcetc000/open-xy/pull/58) read-only: its staging change increases machine space from 508 to 2,044 KiB, taking an additional 1.5 MiB from the general application heap. No SDK callback, lease, pitch/control or machine-ID changes affect this port. Did not pull/rebase/adopt the profile/linker/digest change or raise the existing 520,192-byte gate; upstream's larger reservation is not a measured Rings fit or heavy-load heap guarantee.
- User explicitly authorized redesign and calibration of a consistent modal-identity/reference-validity rule, allowing documented reference-ID changes while retaining sound scope, numeric tolerances, required positives/negatives and self-comparison. Candidate F independently qualifies both sides with the same rule before freezing the reference first 20, retains the cross-side reject-only evidence check, and records every inclusion/exclusion. Its contract is `local/rings-night-001/results/consistent-modal-qualification-20261024-01/contract.json`; experimental calibration/review are active, with no canonical promotion or integer implementation yet.
- User redirected the active Rings work away from strict 1:1 validator engineering: implement a close, reference-informed 48-kHz integer core with the approved 147/160 SRC; keep representative comparisons diagnostic and require human A/B audition before claiming audible similarity. The frozen source matrix remains 2,285/2,286 passing with one unresolved analyzer-identity case, not a completed acceptance gate. The Q31 SRC host proof is complete; integer DSP is not yet implemented. Updated `RINGS_ENGINE_PLAN.md` and `local/rings-night-001/validation/src-feasibility/integer_contract.json`; no new audio proof or listening was performed. Native/release qualification remains blocked at the latest output/host boundary.
- Committed the verified collection-selection prerequisite as `e54eadc` (`feat(sdk): support opt-in machine collections`). A fresh repository-write-contained public check passed: 78 Node tests and 120 Python tests in each normal/`-O` run, with only the six explicitly private-fixture skips. Updated `AGENTS_WSZ.md` with the pragmatic sound goal, source-rate/RNG policy, current write restrictions and frequent milestone commits. Integer numeric/table, three-model DSP and callback/SRC implementation are now assigned concurrently; those files are in progress, not yet compiled or claimed playable. No push or device action.
- Implemented Rings' bounded integer math, source-table generation and single-definition constant banks under `machines/rings/`. Normal and ASan/UBSan numeric smoke checks passed, including a corrected nonzero-damping SVF coefficient regression (`a3 = g² / (1 + gr + g²)`); evidence: `local/rings-night-001/integer-validation/numeric-20261024-02/results.json`. Preserved both source copyright notices and the full MIT disclaimer in generated tables, then rebuilt and exercised the final source (`numeric-20261024-03/results.json`). Public SDK checks again passed 78 Node tests and 120 Python tests in each normal/`-O` run, with six private-fixture skips. The callback/SRC implementation is written but not yet linked with the in-progress model core. Existing LLVM 15 also passed the preview doctor's WebAssembly compile/link/execute check; this is not a Rings preview or native qualification.
- The actual three-model integer Core now compiles and renders stereo audio. The normal and ASan/UBSan runs each rendered 384,000 frames at 48 kHz through models 0→1→2→0 with zero numeric faults and identical raw-output hashes. Fixed an observed negative signed shift in noise scaling with bounded multiplication; retained the failing report and pre-fix source. Evidence: `local/rings-night-001/integer-validation/model-core-20261024-06/results.json`. macOS rejected a forced LeakSanitizer setting, so no leak-check claim is made.
- Exercised the real SDK callbacks: owners 1–256, two-lease stealing, retrigger/reinitialize/double destroy, two stream orderings over 883 moving-input callbacks, zero normal-run allocations, clean ASan/UBSan and bounded output. Host and executed WebAssembly layout probes agree on Voice 8 B, Context 113,440 B and Shared 226,904 B. Evidence: `local/rings-night-001/integer-validation/callbacks/callback-run-20261024-05/results.json` and `layout-wasm-20261024-02/`. Removed a freestanding `<type_traits>` dependency from production headers while retaining the checks in the host harness; used the SDK's `<cstring>` support.
- Registered the real six-control/three-preset Rings synth and its opt-in `profiles/collection-rings.json`; default selection is unchanged. Full previews `local/rings-preview-20261024-03` and `-04` passed the SDK's WebAssembly ABI, fixed-memory, ownership, peak, lifecycle and drawing checks. The actual Chromium surface reports the C++/WASM backend running at 44.1 kHz; initial note output, patch round-trip and panic were exercised. No device/native qualification is inferred.
- Saved twelve four-second original-Part/integer-Core A/B pairs plus actual 44.1-kHz callback captures under `local/rings-night-001/integer-validation/audition/`; no human listening has occurred. The small comparison exposed material sympathetic-mode level/tone differences. Corrected an actual source-equation error (the voicing Squash curve used x⁴ instead of x¹⁶), improving the two default-patch RMS differences to +0.50/−1.68 dB. Bright/damped and sustain patches still differ by about −11.87/−9.43 dB, so their source equations remain under investigation; this is not being presented as accepted audible similarity.
- Retained the corrected twelve-pair rerender at `audition/results-paired-48k-20261024-02/` and twelve callback WAVs at `results-callback-44100-20261024-02/`; `results-callback-proof-b42-20261024-01/results.json` records strict normal/ASan+UBSan callback passes and zero normal-run allocations for that source. Public SDK checks also pass with Rings registered (78 Node, 120 Python in each normal/`-O` run; six private skips). Browser checks exercised fresh signal updates for all three models/outputs, notes 60/67/72, actual preset/encoder controls, patch round-trip and panic on the earlier compiled preview. A/B tail inspection additionally found an unintended near-DC residual around −0.000767 in a damped inharmonic patch; that remaining shared-string-path fault is preserved for correction, not hidden by a silence clamp.
- Corrected a separate SVF conversion defect: integrator states now round once from the raw accumulators instead of doubling already-rounded outputs. The focused nonzero-state regression and eight-second three-model renders pass in strict normal and ASan/UBSan builds (`model-core-20261024-07/` and `-08/`). The twelve-pair rerender (`audition/results-paired-48k-20261024-04/`) still shows the prior DC tail and sympathetic energy gap, so this correction is not claimed to solve them. A narrowly scoped private runtime trace is following the residual through excitation, DC blocking and string-loop states; no output silence clamp or gain fitting is being added.
- Traced the damped inharmonic residual to the common exciter DC blocker's
  rounded feedback: a ±1,200-Q25-LSB deadband fed the string delays. Carrying the
  discarded fractional feedback removes that bias without a gate, clamp or gain
  adjustment. The focused regression settles both signs to zero; the contained
  eight-second ASan/UBSan render passes with all numeric counters zero
  (`local/rings-night-001/integer-validation/model-core-20261024-10/`).
- Host and executed WASM probes now agree on Context 113,448 B and Shared
  226,920 B; updated the declaration accordingly. Strict normal and ASan/UBSan
  callback proofs pass, with zero normal-run allocations (`callbacks/callback-run-20261024-06/`).
  The rebuilt fixed-memory preview `local/rings-preview-20261024-05` also passes.
  Twelve new A/B pairs (`audition/results-paired-48k-20261024-05/`) show the stuck
  damped tail falling from about −62 to −130 dBFS. The sympathetic bright/sustain
  gaps remain −12.49/−9.60 dB, so focused source-flow/coefficient diagnosis
  continues; no human listening or audible-similarity approval is claimed.
- Execution-boundary incident: a worker used bare compiler commands for the DC
  proof instead of the required sandbox. Intended outputs are inside the repo,
  but its compiler path, HOME and TMPDIR were not recorded; outside temporary
  writes cannot be ruled out. Stopped that route and made the rule explicit here
  and in `AGENTS_WSZ.md`; no outside cleanup was attempted. Its timed-out binary
  links the Xcode clang-17 ASan runtime. A fresh explicit CLT-21/SDK-26 build under
  the raw sandbox passes the full proof; the white-box driver's private-access
  macro warning remains visible, while the production callback builds pass
  strict warnings as errors. Failed attempts remain preserved.

- Ran `python3 sdk.py check`: 78 browser-labelled Node tests passed; each
  Python run (normal and `-O`) discovered 113 tests, with 107 successful and
  six private-package cases skipped. Saved raw process output under
  `local/research/outputs/.drafts/audit-2026-10-24-sdk-check.log`.
  Public checks passed; private/native release qualification, device timing,
  listening and recovery were not performed. No firmware/device operations.

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
