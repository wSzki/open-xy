# SDK: per-note velocity for synth machines

Slug: `sdk-voice-velocity`

## Context

The OP-XY carries per-note velocity end-to-end (keyboard → dispatcher `0x8d595738` arg 6 → voice
start), but the SDK's synth voice adapter discards it: `int @PREFIX@_start(void* self)` at voice-object
vtable entry `+16` takes only the handle, so `docs/api.md`'s four synth callbacks never see velocity
and every machine renders at fixed velocity. This plan adds an opt-in velocity hook to the synth
adapter, the same optional-ADL mechanism the drum kit's `finished` hook uses
(`sdk/platform/opxy-1.2.1/drum_voice.cpp.in:79-90`, documented in `docs/api.md:374-384`), plus the
browser-preview wiring so velocity is audible in the workbench (the primary audition surface for
this project's wurli machine and every future synth).

End state: a machine MAY define, in its namespace,

```cpp
void note(Voice& voice, Shared& shared, opxy::u32 velocity);   // 1..127, raw stock units
```

It runs once per note onset, after `initialize`, before `start`, on both begin paths. Machines
without it compile and behave unchanged; no profile patch, no catalog churn, no SDK version bump.
All work happens in `/Users/wsz/.tmp/op-xy-wurli` (the open-xy SDK checkout) on a new branch off
`origin/staging`. The wurli machine's own velocity physics are a separate later task (not this plan).

## Ground facts (verified this session)

- Adapter: `sdk/platform/opxy-1.2.1/voice.cpp.in` — `int @PREFIX@_start(void* self)` (line ~186);
  `begin()` calls `initialize` then `start` (line ~101-102); the in-place restart calls `start` again
  (line ~195); the 11-entry vtable puts `start` at entry 4 (+16), `noop` at entry 5 (+20) (lines ~264-270).
- Handle: `struct Handle { const void* const* vtable; opxy::u16 seen, asked; opxy::u8 slot, wants; }` —
  4+2+2+1+1 = 10 bytes padded to 12 (`static_assert(sizeof(void*) != 4 || sizeof(Handle) == 12)`).
  One `u8` fits in the 2 bytes of padding without growing the struct.
- Sibling evidence stock CAN pass note data beyond `self` at these entries: the sample adapter's
  `start(void* self, opxy::u32 key)` carries the comment "Stock +16 note-start receives the parent
  note in R1" (`sample_voice.cpp.in:107`); the drum adapter's constructor wrapper
  `start_drum(void* self, opxy::u32 key, opxy::u32 /*velocity*/, ...)` at `drum_voice.cpp.in:127-131`
  documents stock's drum voice constructor vtable `+20` at `8d51af48` receiving
  `(self, key, velocity, region, track_parameters)`; the drum entry-4 start receives `key` in R1.
- The dispatcher receives velocity: `sdk_extensions_live_note_on` wraps stock `0x8d595738` with
  `(controller, track, note, a3, a4, velocity, a6, a7, low, high, flags, event)`
  (`extension_stock.cpp:143-151`); its hook site is profile patch `0x8d594376` (profile
  `profiles/opxy-1.2.1.json`, extensions service).
- Existing RE assets: stock OS at
  `~/Library/Application Support/OPXY-SDK/private/stock-1.2.1/os.ldr`; the portamento recon
  `~/Library/Application Support/OPXY-SDK/private/portamento-auto-recon/` (REPORT.md §5.4,
  notes-C-audio.md §2) already maps the dispatcher's routing: per-track voice array stride 0x290 at
  `track+0x108`, 0x20-byte note-map records at `track+0x2a08`, live handler `0x8d594278`.
- Step-components RE (unmerged branch `origin/sdk/step-components`, commit `f87551d`) documents the
  stock note record: MIDI note at `+0x0d`, velocity at `+0x0f` (step_object.cpp.in `kVelocity=0x0f`)
  — the sequencer's per-note record.
- Preview shim is a single TU that includes `engine.cpp`
  (`sdk/host/voice_preview.cpp.in:3`); exports list `VOICE_EXPORTS` in
  `tooling/preview.py:15-18`; `engine.mjs` noteOn at lines 74-88 calls `api.opxy_start(voice.state)`
  (line 83) and applies velocity only as mixer gain (line 117). An optional export already works
  without an ABI bump: `setModel` probes `this.api.opxy_preview_model` (`engine.mjs:71-74`), added by
  staging commit `b6f8c54` which also appended to `VOICE_EXPORTS`. `opxy_preview_abi()` stays 1.
- Optional-ADL precedent and test pattern: `tests/test_synth_release.py` on
  `origin/feat/extended-machine-runtime` (commit `e81fea0`) shows the full recipe — generate_voice +
  generated adapter host test + generated preview shim + wasm export-list test with
  `tools/check_synth_release.mjs`. On staging today, `tests/test_voices.py::run_fixture` generates the
  adapter with `generate_voice` and host-compiles a driver (`tests/fixtures/synth_voice.cpp`, 253
  lines, calls `probe_factory(i)` then vtable entry 4 via `Method(void*)` typedef at line 19).
- Stock address naming: single-unit addresses stay beside the unit's code (`stock.hpp:1-2`); the
  RE-discovered velocity source address goes in `voice.cpp.in` as a local constant (the
  `kTrackParameters`/`kTransposeParameter` pattern in `sample_voice.cpp.in:49-51`).
- Profile digest: `tooling/pipeline.py:config()` (line 118-119) requires
  `sha(profiles/opxy-1.2.1.json) == builder/core.mjs PROFILE_SHA256` — changing the profile REQUIRES
  updating `builder/core.mjs:13`. Strategy C is the only branch that touches the profile.
- Local gates: `python sdk.py check` (CI parity: `.github/workflows/ci.yml` runs exactly this plus
  web tests); `python -m unittest discover tests` runs the python suite; wasm preview builds need
  `/opt/homebrew/opt/llvm@15/bin/clang++` (already the .venv-passing doctor setting this machine).
  Release verification (`sdk.py check --native-package ... --stock-os ...`) is release-host only
  (`windows-x86_64`, enforced `checks.py:62-64`) — a maintainer runs it; the PR states this.
- SDK version convention: `tooling/machines.py:41` `SDK_VERSION = (1, 8)` with a per-version comment
  block above it. An ADL-optional hook needs no gate (machines without it are unaffected); the
  version comment gains one line describing the hook.
- Branch/PR process: branch from `origin/staging`, PR base `staging` (CONTRIBUTING.md); issue first,
  `Closes #N` in the PR. Current checkout sits on a local branch `wurli` holding plan-doc commit
  `fcfe09e` — that branch is never merged and never based on (CONTRIBUTING keeps plans out of the SDK).
  `origin/staging == 598e121 == local commit 598e121`, clean fast-forward.
- git author config: read-only session found no user.name/user.email set — set per-commit (step 0).

## Approach

Ordered so the tree builds and the public suite passes after each numbered step's commit.

### Step 0 — branch and issue

1. `git -C /Users/wsz/.tmp/op-xy-wurli fetch origin && git -C /Users/wsz/.tmp/op-xy-wurli switch -c
   voice-velocity origin/staging` (verbatim: staging head is `598e121`; the working tree is clean; the
   local `wurli` branch stays untouched).
2. Set git identity for the commit series only if unset (`git config user.name` empty →
   `git config user.name "wsz" && git config user.email "wsz@users.noreply.github.com"` — adjust to
   the account that opens the PR).
3. Open the tracking issue with `gh` (repo `etcetc000/open-xy`), title "Expose per-note velocity to
   synth machines", body summarizing: stock carries velocity to the voice start; the adapter drops it;
   the plan is an ADL-optional `note(voice, shared, velocity)` hook mirroring the drum kit's
   `finished`, with browser-preview delivery; recording the loudness-layering note (stock still
   applies its own velocity-derived gain after render — machines should shape character, not level).
   If `gh` is unauthenticated, create the issue via the web UI and record its number.

### Step 1 — RE spike: find where velocity lives at the voice start

Time-boxed; decides which capture branch below the implementation takes. Read-only.

1. Read `~/Library/Application Support/OPXY-SDK/private/portamento-auto-recon/REPORT.md` §5.4 and
   `notes-C-audio.md` §2a/§2b (dispatcher call `8d594376: CALL 0x8d595738 ; DISPATCH (R1=pitch
   B[SP+0x54], R2=B[SP+0x58])` — R2 is unlabeled and is a velocity candidate).
2. Disassemble the stock OS regions with the recon's established tooling (objdump slices of
   `os.ldr`; the recon dir has `syncdis.py`/`build_image.py` producing its code1-4.bin slices and
   `fork-bfin-dis.c` reference). Targets, in order:
   a. The voice-object vtable entry-4 call site: find the caller that invokes `vtable+16` for the
      normal synth path. Anchor chain: dispatcher `0x8d595738` → per-voice handler
      `0x8d5ab7de` (plus `0x8d58cf58`, `0x8d58d1c0`, `0x8d58c250`) → the object call. Record which
      registers hold what at the call (R0 = self confirmed; R1 = ?).
   b. A stock engine's own entry-4 handler (the analog of the SDK's): a stock synth registration's
      method table shows which argument registers the handler reads. This is the decisive evidence:
      if stock engines read velocity from a register argument at entry 4, that register is the answer.
   c. If no register carries it: the drum analog says stock stores per-note data in the voice record
      (drum region index at voice `+0x5c`, stored by `8d51a820`). Find the synth voice record's
      velocity field: trace dispatcher arg 6 forward through `0x8d595738`'s stores (voice array
      stride 0x290 at `track+0x108`; note-map records at `track+0x2a08`).
3. Outcome branch — record the finding in the PR description verbatim (address, register, offset):
   - **Branch A** (a register argument carries note/velocity at entry 4): capture is pure C — widen
     the adapter's start signature; no profile edits. Expected outcome, given the sample adapter's
     documented R1 note and the drum constructor's explicit velocity argument.
   - **Branch B** (velocity stored in a stock structure readable at entry 4): the adapter reads it
     with a local address constant beside the unit's code (the `kTrackParameters` pattern); no
     profile edits.
   - **Branch C** (neither): a new reviewed hook — profile patch replacing the 4-byte CALL at
     `0x8d594376`-style dispatcher site with `sdk_voice_note_on`, plus `builder/core.mjs:13`
     PROFILE_SHA256 update, plus a `voice_hooks.s` wrapper (copy `drum_hooks.s`'s HOOK_SAVE/
     HOOK_RESTORE frame, `hook.inc`) that captures `(track, note, velocity)` into a per-voice-index
     slot published by the stock voice allocation (capture at the `0x8d595882 CALL 0x8d587708`
     allocation return, mapping the allocated index to the pending note's velocity). This branch is
     the heavyweight fallback; if reached, stop and re-confirm with the user before adding profile
     edits, since it changes the reviewed firmware-edit surface.
   The spike does not modify anything; Branch A/B/C is chosen by evidence, and the rest of the plan
   is identical for A and B (only the capture lines in `voice.cpp.in` differ).

### Step 2 — adapter: capture, storage, ADL hook

All edits in `sdk/platform/opxy-1.2.1/voice.cpp.in` unless stated.

1. Handle gains the velocity byte in its padding:
   `struct Handle { const void* const* vtable; opxy::u16 seen, asked; opxy::u8 slot, wants, velocity; };`
   Update the field comment (line ~38-41) to name it. The 12-byte static_assert stays valid — verify
   with the existing `static_assert(sizeof(void*) != 4 || sizeof(Handle) == 12, "Handle layout")`.
2. Widen the stock-called start to what the spike found (Branch A shown; Branch B keeps `(void*)`
   and reads the stock field instead):
   `int @PREFIX@_start(void* self, opxy::u32 note, opxy::u32 velocity)` reading only the arguments
   the spike proved exist; if stock passes fewer, declare only those. Store
   `handle->velocity = velocity & 0x7fu;` (clamp 1..127 upstream guarantees validity; mask keeps a
   garbage register harmless) at the top of `@PREFIX@_start`, BEFORE `pending(handle)` returns, so a
   waiting note's velocity survives until `begin()` runs.
   Also store it on the in-place restart path (line ~195's `@NAMESPACE@::start(slot->voice, shared,
   owner(handle))`): that restart call site passes the handle's just-stored velocity to the new hook.
3. Add the ADL dispatch beside the file's other helpers (copy the `sdk_drum_finished` shape from
   `drum_voice.cpp.in:79-90` verbatim, renamed):

   ```cpp
   // An engine may define `void note(Voice&, Shared&, opxy::u32 velocity)` in its
   // namespace: called once per note onset, after initialize, before start, with
   // the note's raw 1..127 velocity. Argument-dependent lookup finds it, and an
   // exact non-template match wins over this fallback; an engine without one
   // gets no call, so its voices behave as before.
   namespace sdk_voice_note {
   struct Absent {};
   template <class V, class S> void note(V&, S&, opxy::u32) {}
   template <class V, class S, class R> void dispatch(R result) {}   // void result overload
   template <class V, class S> void call(V& voice, S& shared, opxy::u32 velocity) {
       note(voice, shared, velocity);
   }
   }
   ```

   (The drum `finished` precedent returns a value and wraps it; `note` returns void, so the fallback
   is an empty body and the exact-match rule does the opt-in work — simplest correct form, no
   result-channel machinery.)
4. Call it in `begin()` between `initialize` and `start`:
   `sdk_voice_note::call(slot.voice, shared, handle->velocity);`
   and at the in-place restart (step 2.2's site) with the same form. Both calls unconditional — the
   fallback is free for machines without the hook.
5. Zero the byte where handles reset: `@PREFIX@_factory` (line ~210-217) sets
   `handle->velocity = 0;` beside `handle->wants = 0;` so a stale velocity never survives a
   factory cycle.
6. `python sdk.py check` — must pass (it expands the templates per fixture machine; a widened
   signature that stock never calls with populated registers still compiles and host-tests).

### Step 3 — preview: export the hook to the workbench

1. `sdk/host/voice_preview.cpp.in`: add, inside the `extern "C"` block beside `opxy_start`:

   ```cpp
   void opxy_note(Voice* voice, opxy::u32 velocity) { sdk_voice_note::call(*voice, shared, velocity); }
   ```

   The shim includes `engine.cpp` (single TU), so the ADL name resolves; machines without the hook
   get the empty fallback. The export is always emitted (it costs nothing and lets the runner probe
   it uniformly), but its presence alone changes nothing until called.
2. `tooling/preview.py:15-18` `VOICE_EXPORTS`: append `'opxy_note'` after `'opxy_start'` (copy the
   `opxy_preview_model` addition pattern from commit `b6f8c54`). No ABI bump: `opxy_preview_abi()`
   stays `return 1` and `engine.mjs:14`'s check stays `!== 1`.
3. `sdk/browser/engine.mjs` `noteOn` (lines 74-88): after the existing
   `this.api.opxy_start(voice.state);` line add:

   ```js
   if (this.api.opxy_note) this.api.opxy_note(voice.state, Math.max(1, Math.round(velocity * 127)));
   ```

   — optional-probe exactly like `setModel` (line 73), called BEFORE `opxy_start` so the callback
   order matches the device (initialize → note → start). JS velocity is (0,1]; quantize to stock's
   1..127 raw units. Keep the existing mixer-gain `voice.velocity` application (line 117) — it is
   the stock-loudness analog and stays.
4. `python sdk.py check` again (the web test step inside check covers
   `tools/check_preview_contract.mjs` against a generated fixture preview; the new export must not
   break the contract check, which calls `noteOn` at default velocity).

### Step 4 — fixture machine + host adapter test

1. New fixture `tests/fixtures/machines/probevelocity/` modeled on `probesynth` (6 files: machine.json,
   definition.json, core.h, engine.cpp, display.cpp, panel.svg.fragment — skip display.h, which
   probesynth also omits). Contents:
   - `machine.json`: `{"schema": 1, "sdk": "1.1", "kind": "synth", "description": "Test fixture: a
     synth whose voices render their note velocity.", "identity": {"engine": 46, "page": 94}}`
     (engine 46/page 94 verified free: fixtures use 44/45/47 and 92/93/21/7; 46/94 sit inside the
     engine pool `34..255 minus stock_engines` and page pool `82..255`).
   - `definition.json`: one linear percent parameter (copy probesynth's `width` shape), two presets,
     `resources` with `voiceBytes: 4, voiceAlignment: 4, voiceCapacity: 8, sharedBytes: 0,
     outputPeakQ31: 1073741824` and probesynth's `display` block verbatim (it is a valid
     minimal drawing display).
   - `core.h`: `namespace probevelocity { struct Voice { opxy::u32 velocity; }; struct Shared {}; }`
   - `engine.cpp`: `initialize`/`start` zero `voice.velocity`; the optional hook
     `inline void note(Voice& voice, Shared&, opxy::u32 velocity) { voice.velocity = velocity; }`;
     `render` writes `output[i] = static_cast<opxy::i32>(voice.velocity << 16);` — every sample equal,
     so the existing `rendered()`-style helpers read the level directly. Free functions in the
     namespace, `inline`, exactly the fixture-engine shape (probesynth/engine.cpp:6-11).
   - `display.cpp` + `panel.svg.fragment`: copy probesynth's verbatim (its IDs are artwork-local and
     the fixture loader never binds them natively in the adapter test).
2. `tests/machine_fixtures.py`: add `'probevelocity'` to `NAMES` (line 21) and a docstring bullet
   (line 7). This runs it through every generic fixture consumer automatically
   (`test_native_package.py` private suite, catalog shape checks).
3. New test file `tests/test_voice_velocity.py`, copying `tests/test_voices.py`'s `run_fixture`
   harness verbatim (generate_voice + host-compile + run, `HOST_CXX` skip guard) with two changes:
   the generated engine text (its own `CORE`/`ENGINE` strings at lines 15-38 carrying the `note`
   hook and velocity-echo render) and a new driver source. Driver (adapted from
   `tests/fixtures/synth_voice.cpp`'s driver core — `probe_factory`, `Method`/`Render` typedefs,
   `call(index, entry)`, `rendered(index)`; do NOT copy the whole 253-line scenario):
   - Widen the driver's start typedef to what step 2 declared and pass distinct note/velocity words,
     e.g. `start(h, 60u, 100u)` then `start(h, 60u, 40u)`.
   - Assert: no sound before start (`rendered == 0`); after the first start,
     `rendered() == (100 << 16) >> 16` per the fixture's echo; after re-start with 40, level follows
     40 (hook fired again on the restart path); a machine variant WITHOUT the `note` function
     (compile the same driver against an engine text lacking `note`) renders a level independent of
     the passed velocity (fallback no-op, existing behavior unchanged).
   - Also cover `opxy_note` end-to-end: in the same file, a second test builds the preview shim
     (`tooling/adapters.generate_voice_preview`), compiles it to wasm with the
     `VOICE_EXPORTS` list (copy `tests/test_synth_release.py:128-137`'s compile+node-check pattern;
     write the small `tools/check_voice_velocity.mjs` modeled on that commit's
     `tools/check_synth_release.mjs`: construct `PreviewEngine`, `noteOn(60, 0.25)` vs
     `noteOn(60, 1.0)` on a fresh engine each, render once, assert the two blocks' first-sample levels
     differ in the expected 32:127-ish ratio via `engine.voices[0].output[0]`), and skips cleanly
     when clang++/node are absent (the same `skipUnless` guard shape).
4. `python -m unittest tests.test_voice_velocity -v` from the repo root — both tests pass.
5. `python -m unittest discover tests` — whole suite green (this is CI parity for everything except
   the native-only private fixtures, which skip without the env vars).

### Step 5 — docs

1. `docs/api.md`, the Lifecycle section (lines 256-283): after the four-callback code block add:

   ```cpp
   void note(Voice& voice, Shared& shared, opxy::u32 velocity);   // optional; in the machine namespace
   ```

   plus one short paragraph in the `finished`-hook register (copy `docs/api.md:374-384`'s tone):
   called once per note onset between `initialize` and `start`, on every begin path including a
   same-index restart; `velocity` is the note's raw 1..127 value as stock's dispatcher received it;
   a machine without it is never called and behaves exactly as before. State the layering rule
   explicitly: stock still applies its own velocity-derived gain, envelope, filter and mixing after
   `render` (abi-1.2.1.md:265), so the hook should shape character — strike physics, spectrum,
   noise, brightness — and leave steady-state loudness to stock, or the two curves double-apply.
2. `docs/api.md`, the browser-preview sentence inside Lifecycle (line ~280: "The browser preview's
   voice stealing and panic use the same callbacks…"): extend with ", and a previewed synth's
   optional `note` hook receives the keyboard or phrase velocity, quantized to 1..127".
3. `docs/abi-1.2.1.md`, Synth voices section (lines 228-266): after the sentence "Render receives
   64 output words and 8 parameter words…" (line 265), add the note-start facts the spike
   established: what stock passes at vtable entry +16 (register/argument inventory with the
   addresses found in step 1, named once here, mirroring how the drum transport section documents
   `+20` at `8d51af48`), and that the adapter stores the velocity per handle and forwards it through
   the optional hook. One paragraph; no second status page.
4. `docs/preview.md`, the exports paragraph (lines ~92-96: "Preview modules expose ABI version 1,
   compiled state/buffer sizes, static voice addresses and lifecycle/render exports."): append
   "and the optional velocity export `opxy_note`" with one clause on how the runner probes it.
5. `tooling/machines.py:30-41`: append to the version-comment block:
   `# 1.8 also lets a synth declare an optional note(voice, shared, velocity) hook.`
   No `SDK_VERSION` change (opt-in source-level detection).

### Step 6 — commit, push, PR

1. Split into reviewable commits on `voice-velocity` (the repo's message register is imperative,
   subject ≤ ~70 chars, body names behavior and evidence; see `b6f8c54`/`8c0df8c`):
   - `feat: velocity` scaffolding order is wrong — actual split:
     a. `Record the velocity stock passes a synth voice start` — step 2 only (adapter), with the
        spike's finding summarized in the body.
     b. `Preview velocity: the workbench forwards a machine's note hook` — step 3 (shim, exports,
        engine.mjs).
     c. `Test the velocity hook on the adapter and in the browser` — step 4 (fixture, tests, checker).
     d. `Document the synth velocity hook` — step 5 (four docs + machines.py comment).
2. Before each commit: `python sdk.py check` green; `python tools/check_private_files.py --staged`
   green (CONTRIBUTING's guard; the spike read private paths but never writes any).
3. `git push -u origin voice-velocity`; open the PR against `etcetc000/open-xy:staging` with
   `Closes #<issue>`; body covers: behavior (the hook, when it fires, what stock still does after
   render), how to try it (the probevelocity preview via `python sdk.py preview --machine
   tests/fixtures/machines/probevelocity --out local/velocity-preview-001 --cxx
   /opt/homebrew/opt/llvm@15/bin/clang++` then `python tools/serve_harness.py --preview
   local/velocity-preview-001`, drag the Velocity-echo output by playing keys at different
   velocities via `machineLab.noteOn(60, v)`), compatibility (no machine changes required; no
   version bump; old previews in new workbenches and new previews in old workbenches both
   unaffected), checks run (`python sdk.py check`, full unittest suite, wasm contract check), and
   the note that release verification runs on the maintainer's Windows release host per
   docs/build.md#release-verification (this machine is darwin-arm64 and the step is maintainer-side).

## Verification

Every gate below is a concrete command with an expected observable.

1. RE finding recorded (step 1): the PR description quotes the stock address/register/offset that
   carries velocity at the synth voice start, with the disassembly line. No gate passes without it
   (Branch C additionally requires the user's explicit go-ahead before profile edits).
2. Adapter unit proof (step 4.4):
   `cd /Users/wsz/.tmp/op-xy-wurli && python -m unittest tests.test_voice_velocity -v`
   → `test_velocity_reaches_the_hook ... ok` and `test_without_the_hook_behaviour_is_unchanged ... ok`.
3. Whole suite (CI parity):
   `python -m unittest discover tests` → `OK` (skips limited to the documented private fixtures).
4. Public gate: `python sdk.py check` → `PASS: public source checks; private tests explicitly
   skipped.` (CI runs exactly this; it includes the browser-tests and preview-contract steps.)
5. Browser end-to-end (the actual listening surface):
   `python sdk.py preview --machine tests/fixtures/machines/probevelocity --out
   local/velocity-preview-001 --cxx /opt/homebrew/opt/llvm@15/bin/clang++`
   then `python tools/serve_harness.py --preview local/velocity-preview-001` (port 8917) and, in a
   driven Chromium tab, `await machineLab.noteOn(60, 0.2)`, render, read `engine.voices[0].output[0]`;
   repeat at `1.0`. Expected: the two levels differ at roughly the 25:127 ratio of the quantized
   velocities (fixture echoes `velocity << 16`), and `await tab.errors()` is empty. Use the
   dedicated-Chromium workaround for the 44.1 kHz requirement (managed-Chromium audio reports 48 kHz:
   launch `/Applications/Chromium.app/Contents/MacOS/Chromium` with
   `--user-data-dir=/tmp/wurli-chrome-profile --autoplay-policy=no-user-gesture-required`).
6. wurli pre-flight (proves the real machine still builds against the changed SDK; no wurli edits):
   `.venv/bin/python sdk.py test --machine /Users/wsz/.tmp/wurli --cxx
   /opt/homebrew/opt/llvm@15/bin/clang++ --host-cxx c++` → all sound checks pass, CPU estimate
   unchanged (~13,600), proving the widened start signature and new export are backward-compatible
   for an existing machine.
7. Native-compile honesty gate: the pinned `bfin-elf-g++` is not provisioned on this machine and
   `sdk.py toolchain fetch` publishes windows/darwin/linux bundles whose darwin-arm64 bundle
   (`profiles/toolchains.json` hosts.darwin-arm64) MAY provision locally — attempt
   `python sdk.py toolchain fetch && python sdk.py doctor --toolchain`; if it passes, also run
   `python sdk.py compile --machine tests/fixtures/machines/probevelocity --out local/velocity-module-001`
   (module-only compile needs no stock files, per `native.py` COMMANDS) to catch Blackfin-side
   compile errors before the PR. If the toolchain will not provision, state in the PR that the
   Blackfin compile ran only in CI/release verification.

## Assumptions & contingencies

- The spike lands Branch A or B (register argument or readable stock field) — both need zero profile
  edits; the plan's implementation steps are written against Branch A with the Branch B delta called
  out inline (step 2.2). If the evidence forces Branch C (a new reviewed profile hook), STOP and
  confirm with the user before touching `profiles/opxy-1.2.1.json` and `builder/core.mjs:13`;
  the remaining steps then apply unchanged on top.
- If stock passes nothing usable at entry 4 AND stores nothing reachable (both A and B fail), the
  fallback within Branch C is the allocation-return capture (`0x8d595882 CALL 0x8d587708` maps the
  allocated voice index to the dispatcher's still-live velocity); if even that is unresolvable
  statically, the velocity work is parked and the PR is not opened — report the finding instead.
- The browser preview's AudioContext on this machine needs the dedicated-Chromium launch (managed
  Chromium ignores the 44.1 kHz request); verification step 5 embeds the workaround.
- `gh` is assumed authenticated for issue + PR creation; otherwise the web UI substitutes (step 0.3,
  step 6.3) and the plan continues unchanged.
- The `wurli` local branch (plan-doc commit `fcfe09e`) is never merged, rebased onto, or deleted by
  this plan; it is session-local history.