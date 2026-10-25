# Rings as the first OP-XY engine

## Context

Planning authorizes implementation of the complete device-free integer Rings port. Keep authentication, protected ranges, occupied-ID guards and full reconstruction unchanged. Native firmware and firmware-containing outputs stay outside the checkout; no device operation is authorized.

## Grounded findings

- Repository README.md:43–47 defines a machine as metadata, controls, C++ sound/display and artwork; sdk/ owns adapters, storage, previews and builds.
- CONTRIBUTING.md:7–17 requires engine DSP/controls/artwork under its own machine folder, with no engine-driven tooling additions.
- CONTRIBUTING.md:39–43 separates public SDK checks from machine sound verification and requires complete private-fixture release verification for native changes.
- Fork research outputs are ignored under local/ and closed artifacts must be preserved; current corrected research supersedes the historical research.
- Existing corrected audit identifies 168,960 bytes of Rings storage components, not a complete Rings footprint; measured native CPU availability remains unknown.

## Product decisions

- User-selected sound set: the three standard models — Modal, Sympathetic Strings and Inharmonic String — without hidden models or StringSynthPart.
- User-selected DSP goal: a close, reference-informed port; exact source operation order, binary32 rounding, RNG sample mapping, waveform identity and bitwise agreement are not delivery requirements. Preserve the character and useful behavior of Rings rather than optimizing the validator.
- User-selected note capacity: two independently sounding note contexts across all Rings instances/tracks; a third note replaces the oldest context. Each context retains the full eight-string sympathetic bank; this is separate from note polyphony.
- Design call: use an input-free synth with internal exciter; OP-XY's stock velocity/envelope/filter/mixer remain downstream owners.
- Coherent source pins: eurorack `f4cfa7697095a28d292bff758beb51deb49b5a86`; its stmlib submodule is `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec`. Preserve notices and file SHA manifests.
- Upstream standard-mode processing: internal polyphony 1 yields 60 modal filters or eight sympathetic strings, while inharmonic mode uses one dispersive string. Reverb belongs to the unselected STRING_AND_REVERB model and is out of scope.
- User-selected collection: add a dedicated opt-in Rings-only collection; leave `profiles/collection.json` and bundled engine resources unchanged.
- `tooling/native.py:290–293` links object files directly with no runtime libraries. Use bounded integer/LUT arithmetic in the native path, not a soft-float runtime; external float references are for comparative diagnostics.
- `sdk/host/voice_preview.cpp.in:10–21` allocates eight lightweight Voice objects and one Shared. Store both full contexts in Shared rather than duplicating them per Voice.
- `sdk/include/opxy/lease_pool.hpp:16–35` supplies bounded, ownership-checked oldest-slot replacement. Lease entire contexts; context string indices remain 0–7.
- `sdk/host/voice_preview.cpp.in:10–21` allocates eight lightweight Voice objects and one Shared in the browser. Store both full contexts in Shared rather than duplicating a roughly 100-KB context eight times.
- `sdk/include/opxy/lease_pool.hpp:16–35` supplies bounded, ownership-checked oldest-slot replacement. Lease entire contexts; each context keeps string indices 0–7 intact, so per-string LFO roles do not depend on lease slot.

## Approach

### 1. Enable opt-in collections without changing the default

The user explicitly authorized this shared-tooling prerequisite despite the fork's usual upstream-file restriction. It changes machine selection only: do not alter the authenticated firmware profile, linker memory reservation, stock/tool hashes, patch allowlist, ABI, kit schema or protected ranges.

Keep `tooling/machines.py`'s `COLLECTION` Path constant. Add `collection_file(path=None) -> str`, returning a normalized repository-relative POSIX `.json` filename; None selects the existing constant. Reuse `tooling.pipeline.child(ROOT, relative)` to reject absolute/traversal/backslash/colon and outside-root symlink paths, then require a regular existing `.json` file. Non-string explicit values raise `ValueError("Collection must be a repository-relative .json file")`; missing files raise `ValueError("Missing collection file: " + relative)`. `collection(path=None)` loads that file, requires exactly `{"schema": 1, "machines": [...]}`, and retains existing nonempty/distinct-machine, occupied-ID/conflict and analysis-budget checks. Invalid JSON/schema/unknown machine fails before compilation; there is no fallback to default.

Thread a final `collection_path=None` argument through these existing functions and every caller, retaining other argument order and `verification`'s keyword-only `require_record`:

- `tooling/native.py`: `configuration`, `provenance`, `compile_collection`, `build`, `verification`; both pre/post compilation provenance checks use the same selected path. Replace the hardcoded default provenance entry with the normalized selected path. `main` accepts `--collection` for compile/build/verify and passes it throughout.
- `tooling/browser_kit.py`: add `collection_path=None` as a keyword-only `export` argument; select the native configuration with it, remove its redundant default-recipe read, and have `main` pass `--collection` into native build, internal native check and export. Existing `profileSource` and kit fields remain unchanged: browser kits already represent their actual selected machines.
- `tooling/checks.py`: accept `--collection`; native mode resolves it and sets `OPXY_TEST_COLLECTION` for both Python suites and forwards it to the independent `sdk.py verify`. A collection argument alone is not native qualification: fail with `Collection selection requires all native qualification inputs`. Keep all private fixtures required, environment stripping and no-skipped-tests rule.
- `tests/test_native_package.py`: load `native.configuration(os.environ.get("OPXY_TEST_COLLECTION"))` and pass that same selection through its existing cached-compiler verification helper. Do not weaken any attack/ownership/fixture test.

Before editing exported signatures, use available LSP references (or exact symbol searches if no Python LSP exists) to cover their callers, including tests. Delete the incidental discovery-equals-default/order-only test in `tests/test_machines.py:21–25`; do not repin it to five machines or replace it with a hardcoded published order. Discovery and selection are now different concepts. Keep behavioral identity checks; add loader-boundary tests for selected membership, malformed/empty/duplicate/unknown selection, conflicting identities and unsafe paths using isolated temporary repo-contained fixtures. Preserve public-mode stripping of a stale `OPXY_TEST_COLLECTION`. Do not add forwarding/mock-echo or source-text tests.

Use a temporary FM2op-only recipe to prove selection before Rings exists. Add `profiles/collection-rings.json` with exactly `{"schema": 1, "machines": ["rings"]}` as part of integrating the complete machine. Never add Rings to `profiles/collection.json`. Selection infrastructure can be developed independently of the numerical kernels.

### 2. Retain source provenance and use a practical reference

Vendor the portable three-model dependency closure from the coherent eurorack/stmlib pins above, with original notices, source SHA-256 records and explicit `OP-XY:` patch comments matching Macro Osc conventions. Keep upstream reference material separate from the integer adaptation. Do not bring over ARM startup/board code, float-runtime dependencies, reverb, FM or string-synth branches. Record retained and adapted files; upstream is a useful sound reference, not a required operation-by-operation oracle.

### 3. Implement two leased full contexts behind the existing synth callbacks

Use `opxy::LeasePool<Context, 2>` in `Shared`. A context owns one 60-mode modal bank, eight string delay pairs (2,048 main + 1,024 dispersion words each), one 256-word plucker line, all associated state, RNG, 24-frame scratch and output scheduling. Keep `Voice` as a lightweight lease handle and pending-strike state. Do not change `kMaxPolyphony` in upstream and accidentally reduce `kNumStrings` from eight to two; the adaptation explicitly fixes internal polyphony to one, modal resolution to 60 and sympathetic count to eight.

Implement exactly the existing `initialize`, `start`, `destroy`, `render` signatures in `docs/api.md:154–158`, without changing adapters. Initialize by releasing any still-owned old lease before clearing the lightweight Voice; do not acquire a context. Acquire/touch a context only on `start`; a new owner receives a cleared free/oldest context. Preserve an already-owned context on retrigger and set a one-shot strike. Each render uses `get(owner, lease)`; an invalid/stolen lease writes all 64 zeros and never reacquires automatically. Destroy releases only a still-owned context and clears its lease, so repeat destruction is safe. Age means most recent `start`, including retrigger, not render order. This avoids stale pointers, cross-track interference and repeated stealing between old rendering voices.

On newly zeroed storage initialize metadata/coefficients without clearing the same 100-KB histories a second time. Model changes reset the newly selected resonator/string state, following the upstream dirty-model behavior, not a crossfade between different algorithms. Mutable histories and RNG never leak into another owner.

### 4. Keep the approved 48-kHz core and convert to the 44.1-kHz ABI

Implement Rings at its original 48-kHz core rate, with its 24-frame processing ticks, then use the already-proven bounded 147/160 streaming SRC to produce 44.1-kHz output. Preserve the existing Q31 SRC implementation and its converter/host proof; integrate one selected-mono history per context. Keep phase, callback scheduling and output bounds explicit. Do not revive the superseded direct-44.1-kHz core or require a new exact float-rate-adaptation pass before DSP implementation.

### 5. Define the instrument controls and mono output

Define six parameters in this exact order; amount mappings are linear [0,1] with percent display:

| Slot | id / label | Default | Position / enum values |
|---|---|---|---|
| 0 | `structure` / `Struct` | 0.25 | Primary 0 |
| 1 | `brightness` / `Bright` | 0.5 | Primary 1 |
| 2 | `damping` / `Damping` | 0.3 | Primary 2 |
| 3 | `position` / `Position` | 0.25 | Primary 3 |
| 4 | `model` / `Model` | 0 | Alternate `control: 0`; `modal`, `sympat`, `inharm` |
| 5 | `output` / `Output` | 0.5 | Alternate `control: 1`; `out`, `mix`, `aux` |

Use the SDK's Q31-to-Q15 enum quantization, matching `machines/macroosc/display.cpp:35–38`, not floating-point rounding. Default output is Mix: `0.25 * (out + aux)` after the upstream limiter; Out and Aux are `0.5 * out` and `0.5 * aux`. Clamp the final Q31 buffer to `[-1073741823, 1073741823]` and declare `outputPeakQ31: 1073741823`. Do not silently discard half the modal bank by making Out-only the default.

Use internal excitation, no external audio/CV or strummer/onset detector. The oracle-equivalent performance state is `tonic = played MIDI note`, `note = 0`, `fm = 0`, `internal_exciter = true`; each integer context takes its base frequency directly from the SDK phase increment sampled at a tick boundary. All sympathetic intervals are relative to that same played frequency, not a second addition of the MIDI note. The upstream NoteFilter sees constant zero in this policy and can be omitted with its exact zero-output equivalence checked; do not add another glide or log-frequency conversion. Stock applies velocity, envelope, filter and mixing after rendering; there is no fabricated note-off callback or additional note amplitude envelope.

Create three presets with all six fields: `Modal` uses the defaults; `Sympathetic` changes only `model` to 0.5; `Inharmonic` changes only `model` to 1. Use `kind: synth`, machine id/namespace `rings`, engine ID 1 and page 84, after rechecking no newly discovered machine occupies either value before publication. If either is now occupied, stop rather than stealing another registration.

The Rings display uses the shared C++ drawing contract: three solid strings,
horizontal at Structure 0 and tilting as Structure rises, spaced by Position.
Brightness scales string length (both endpoints symmetrically) without changing
the constant stroke; Damping selects one of five shades from dark grey at 0
toward white at 100. Note strikes trigger the brief visualization timer across
all three models; this is not PCM-synchronized and makes no audible ring-out
guarantee. It draws via `machines/rings/display.h`'s
`Frame = opxy::draw::Frame<3, 32, 3, 5>` and a 44-byte `DisplayState`; the
native display reserves a numeric header area and vertex coordinates must stay
within the numeric SVG constraints. Browser presentation, monitor-gain protocol
and visible-monitor policy are specified in the Rings README.

### 6. Implement the integer DSP with a reference-informed sound loop

The engine's compiled sources now live under `machines/rings/`: `core.h`,
`dsp.hpp`, `model.hpp`/`model.cpp`, `engine.cpp`, `display.h`/`display.cpp`,
`src.hpp` and the generated `tables.hpp`/`tables.cpp` (regenerable via
`tools/generate_tables.py`), alongside `definition.json`, `machine.json`,
`panel.svg.fragment` and the retained portable `vendor/` closure. The integer
core is implemented and validated; adapt remaining sound decisions against the
reference, not as a bit-exact specification.

The following representations are a concrete starting point for implementation, not a mandate to reproduce the source's arithmetic path. Keep these Q formats and value ranges when they fit the chosen core; any deliberate deviation must preserve safe bounded arithmetic and the sound/usefulness of the instrument.

| Quantity | Storage / scale |
|---|---|
| String/plucker histories, SVF state and ordinary audio | signed 32-bit, 25 fractional bits; range [-64,64) |
| Modal/string sums, bridge state and intermediate differences | signed 64-bit, 25 fractional bits; never narrow before the limiter/coupling scale |
| Base normalized frequency | SDK unsigned phase increment, 32 fractional bits; do not discard eight pitch bits |
| Delay coordinates | signed 64-bit, 20 fractional bits; keep 2,048/1,024/256-word line lengths and source clamps |
| Unipolar parameters and normalized SVF coefficients | unsigned 32-bit, 31 fractional bits, including exact 1 = `0x80000000` |
| Signed unit gains/window values | signed 32-bit, 29 fractional bits, allowing coefficients outside [-1,1] |
| `g` from the source tangent approximation | unsigned 32-bit, 24 fractional bits |
| `r = 1/Q` | unsigned 32-bit, 30 fractional bits, including exact 2 |
| Modal Q lookup | unsigned 32-bit, 8 fractional bits; generator rejects out-of-range values |
| Stiffness and signed shape/window coefficients | signed 32-bit, 29 fractional bits; stiffness endpoint 2.0 must be representable |
| Limiter peak envelope | nonnegative 64-bit, 28 fractional bits; initialized to exactly 0.5 |

All multiply/add chains widen before arithmetic. Use defined signed arithmetic, explicit rounding at narrowed results, unsigned-magnitude handling where needed, and deliberate unsigned wrap only where intended. Prevent overflow, invalid shifts/division, out-of-bounds access and nonfinite/invalid generated coefficients. Any output clamp must be explicit and remain within the existing ABI bounds.

Use bounded integer arithmetic and tables in the native path; no OS code, inline assembly, compiler-runtime dependency or per-sample float. Keep implementation-specific arithmetic choices understandable and locally documented. The exact quantized-rational divider and source binary32 rounding cells are optional techniques, not mandatory matching requirements.

For SVF LP/BP outputs use the algebraically normalized topology, not `g` as a Q31 coefficient. With `h=1/(1+r*g+g*g)`, `a2=g*h`, `a3=g*g*h` and prior states `s1/s2`, implement the expanded form so a 33-bit `input-s2` is never fed to a 32×32 helper:

```text
bp = h*old_s1 + a2*input - a2*old_s2
lp = (1-a3)*old_s2 + a2*old_s1 + a3*input
new_s1 = 2*bp - old_s1
new_s2 = 2*lp - old_s2
```

The normalized SVF equations and checked widened accumulators above are useful guidance for constructing a stable fixed-point filter. They are not a demand for exact source evaluation order or a prescribed rounding fingerprint. Choose representations and intermediates that prove all operations are defined and bounded; never use overflow, undefined behavior, arbitrary sonic clamps or fallback coefficients as a substitute for sound design.

Likewise, the source tangent branches, excitation-kick cancellation, string interpolation/allpass recipes, limiter shape and sample-rate-dependent coefficients are reference-informed starting points. Keep the full model resolution and line lengths, but adapt details where a clear, safe integer design better serves a close port. Exact coefficient words, exact per-operation float behavior, exact RNG word-to-sample rounding and read-only modal-coefficient/identity observers are not shipping prerequisites. Keep deterministic per-context state and avoid cross-owner interference.

### 7. Generate only needed constants and admit the complete machine

Keep the two source pins and MIT notices in `machines/rings/vendor/VENDOR.json` / `VENDOR.md` using the Macro Osc upstream/patched/hash convention, recording the stmlib repository pin explicitly. Engine-owned tables and generators must stay deterministic, bounded, and safe; they need not preserve exact source lookup indexing or binary32 rounding.

Keep constants at sufficient precision for stable fixed-point behavior. Every endpoint and table access must be defined and in bounds; a 257-entry shape-table lookup at normalized 1 must return element 256 directly, never read a nonexistent successor. Exact binary32 conversion, pitch-index truncation, source-polynomial error limits and per-operation source rounding are not admission criteria.
Set `Voice` to `{opxy::u32 lease; opxy::u32 strike_pending;}` and declare `voiceCapacity: 256`; this preserves stable stock owner indices while the two-resource pool enforces the user-visible note limit. `Shared` holds only the two complete contexts and lease metadata. Determine `sharedBytes` from the actual compiled layout and require existing native/WASM size assertions to agree; do not pad declarations to conceal a mismatch.

Add `machine.json` only when all three kernels, callbacks and display are real and validated. List the retained compiled translation units/includes explicitly using the Macro Osc schema; use `rings::dsp` for adapted internals to avoid unintended stmlib/global-symbol collisions. Add `profiles/collection-rings.json` in the same integration step. The two string-bank payloads alone are exactly `2*8*(2048+1024)*4 = 196,608 B`; this is not the full state or link footprint. Admission requires the measured full dedicated collection ≤520,192 B and preview within 524,288 B with its existing stack, without changing either limit.

## Verification

### Reference-informed sound review and safety

Use representative reference-informed cases across the three models, controls, pitches and decay behaviors. Compare rendered output with the pinned float reference and save useful A/B audio. Report waveform, level, spectral, pitch and decay observations as diagnostics that guide improvement; do not use hard waveform/bitwise, cents, NRMSE, peak-error, band-energy, RT60, first-20-partial or fixed case-count thresholds as admission gates. No particular diagnostic pipeline, matrix size, observer machinery, or matching-float exactness is required to implement the instrument. A human must audition the saved representative A/B audio before making an audible-similarity claim; numeric measurements alone do not establish that claim.

Keep host sanitizer and arithmetic checks focused on safety: no undefined behavior, out-of-bounds access, overflow, invalid division, invalid coefficients, or nonfinite values. Verify deterministic operation where practical, all output samples are written and bounded, and callbacks allocate nothing or block. These are safety/runtime properties, not a demand for identical output bits.

### Lifecycle, scheduling and real browser proof

The external runner must call the actual C++ lifecycle, not a substitute pool:

- Start owners 1 and 2 at notes 60 and 67, render interleaved, then start owner 3 at 72. Owner 1's next render is exactly 64 zeros; owners 2/3 continue their own reference streams. Reverse render order before the steal and verify each owner's output is unchanged.
- Retrigger owner 1 before starting owner 3: owner 2 is now the oldest and becomes silent. Call destroy on the stolen owner twice; the surviving owners' histories/leases are unchanged. Reinitialize an active owner and verify it releases its old lease.
- Exercise owners 1..256, not only the preview's eight slots. A lost lease never steals back merely because its old owner still renders.
- Run moving pitch and continuous-control ramps, model changes and note strikes at all reachable callback/tick alignments: the fixed 64/24 cadence cycles through phase 0,16,8. Separately exercise scheduler remainder counts 0..23 at its internal boundary, without claiming 24 distinct public callback event phases. One or more starts before a tick coalesce into one pending strike (the API supplies no timestamp/queue); consume it exactly once. No 16-frame tick appears at callback ends; events wait at most 23 frames. All callbacks write every sample. A model change resets only its owned context's selected DSP, not the other context/RNG.
- Count heap allocations around initialize/start/render/destroy in the host runner; require zero after runner startup. Record worst-case reset/control-change work separately from steady state; these host times are not native cycles.

From `/Users/wsz/.tmp/open-xy`:

```sh
python3 sdk.py doctor --preview
python3 sdk.py preview --engine rings --out local/rings-preview-001
python3 tools/serve_harness.py --preview local/rings-preview-001
```

Use a fresh output suffix if the directory already exists. Through the actual browser at `http://127.0.0.1:8917/`, enable audio and use the documented `window.machineLab` methods to play notes 60/67/72, change all three models and output selections, exercise encoder alternates, export/import a patch and panic. Observe the three-string display and top-bar controls/values, plus waveform as a secondary diagnostic; retain the existing patch, MIDI and safety conventions. No claim of audible quality from RMS alone: save/listen to representative host/browser output and report who actually listened. Browser gating/mixing differs from stock, so use the C++ runner for exact voice-stream assertions.

### Collection, native link and package proof

After integrating the complete change, run `python3 sdk.py check` once, and the new collection-loader behavioral tests in its existing suite. A public pass may skip private package fixtures; it is not release qualification. Shared selection acceptance must additionally exercise real commands:

1. Before the Rings registration, compile the unchanged default and a temporary FM2op-only recipe with the selector. Inspect the selected ELF/object ownership/source report, not a catalog (standalone compile does not produce one). Compile default with omitted versus explicit `--collection profiles/collection.json`: module bytes and selected machine order match; full report hashes may differ from pre-change reports because tooling source provenance changed.
2. Compile the finished Rings-only collection; require the complete link, source authentication, callback recursion/stack, reservations and declared sizes to pass. Inspect undefined symbols: no soft-float/libm/compiler-runtime symbols may be left unresolved. Report total/per-owner code, constants, BSS, maximum audio stack and remaining module bytes; do not call raw string-array subtraction “free memory.”
3. Build and release-verify Rings with the same selector. Inspect `candidate/content/op1_factory.json`: only the selected Rings machine is added to stock, with its frozen engine/page/name and six control defaults. Verify that using the default/wrong selector on that Rings package fails rather than silently substituting machines; also alter a copy of the selected recipe and require verification failure. Never modify the known-good package.
4. Export a Rings-only personal browser kit using the selector and exercise its actual existing GNU-ld parity/firmware checks. The kit exposes Rings only; its full selection reproduces its native candidate. Do not change the kit format, `profileSource` or reviewed firmware-profile hash.

Native compiler outputs are currently deferred because they must be written outside this checkout, which the latest write boundary forbids. Stock inputs and the required Windows release host are also unavailable in the current workspace. Do not run the commands or create native/package outputs until those constraints are lifted. The above compile/link/stack/layout/package checks remain required when qualification is authorized; until then, report native/release qualification as blocked, not passed. Do not install, publish or modify the known-good package.

## Critical files & anchors

- `tooling/native.py:57–79,225–235,630–733`: selected configuration/provenance must agree across source rebuild and package comparison.
- `tooling/checks.py:20–39,75–85`: stale environment stripping, explicit selection propagation, all-or-none private inputs and no-skips gate.
- `sdk/include/opxy/lease_pool.hpp:16–35`: same-owner touch versus cleared oldest replacement; loss checks belong on every render.
- `sdk/host/voice_preview.cpp.in:5–21`: fixed eight Voice objects and one Shared; exact native/WASM sizes cannot be guessed.
- `machines/macroosc/vendor/VENDOR.md:3–18`: file-level notices/pins/patch provenance, not permission to reuse its Braids DSP or rate-table recipe blindly.

## Assumptions & contingencies

- User selected the three standard models, two independent full contexts, eight-string bank, opt-in collection, original 48-kHz core and approved 147/160 output SRC. Preserve the exact controls, rates, final output amplitude policy and registration/default-collection boundaries specified above.
- The SRC host proof does not prove Rings DSP fidelity or resource fit. Implement the integer core; use representative reference comparisons and human audition to inform the close-port result rather than gating implementation on obsolete 44.1-kHz or exactness thresholds.
- Resource fit is measured from the actual compiled layout. Keep the native 520,192-byte allocated-span and preview 524,288-byte limits unchanged. Native/release qualification remains blocked until the required real builds, stock inputs and Windows release host are available; do not imply that host or SRC evidence qualifies them.
- No hardware write, install, flash, reset or recovery experiment is included in this authorization.

