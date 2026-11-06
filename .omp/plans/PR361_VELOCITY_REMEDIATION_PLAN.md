# PR #361 velocity-hook remediation (etcetc000/open-xy)

## Context

PR #361 ("Expose per-note velocity to synth machines", head `fork/voice-velocity` at
`f0e7ce9`, base `staging`) got CHANGES_REQUESTED from maintainer etcetc000
(2026-11-05). This plan implements every blocking and should-fix item. Blocker 1
(+16 register proof) stays hardware-gated: the PR goes back to draft, the ABI doc
claim is removed, and a hardware probe build is prepared for the user to run.

Hard constraint from the user: **commits are allowed; pushes and any PR or issue
mutation (draft toggle, body edit, comments) require the user's explicit
validation.** Steps 9's remote actions run only after the user approves the
finished commits.

Ground truth established this session:
- PR head is `f0e7ce9` (8 commits on merge base `5112f14`); staging tip is
  `2185ec8` (SDK 1.10, 120 files changed since the base). Only `docs/abi-1.2.1.md`,
  `docs/api.md`, `tooling/machines.py` are touched by both sides.
- Local worktree `/Users/wsz/.tmp/sdk-velocity` holds a stale, rewritten
  predecessor lineage of the same work (branch `voice-velocity` at `735816b`).
  The fork head supersedes it; nothing unique is lost.
- `generate_voice_preview(directory, definition, *, kind='synth')` in
  `tooling/adapters.py` already injects kind-specific code via placeholders
  (`DRUM_SELECT` precedent).
- Staging test convention for wasm-needing tests:
  `test_custom_preview.py:193` gates on `shutil.which('clang++') and
  shutil.which('node')` and calls `clang++`; the release host (windows-x86_64,
  `profiles/toolchains.json`) therefore already requires clang++ + node on PATH
  for its own suite, so the new wasm test adds no release-host requirement.
- `tooling/preview_doctor.probe(compiler, node)` builds AND runs a wasm module,
  raising `ValueError` with advice when unusable — reuse it, never re-implement.
- `tooling/cpu.py` SYNTH harness compiles with `-I sdk/include` + machine root;
  `sdk/templates/synth{,-eight}/tests/sound.cpp.in` are starter templates shipped
  to machine repos (SDK never compiles them); device builds put `sdk/include` on
  the include path (`tooling/native.py:171`).
- The SDK is currently (1, 10) in `tooling/machines.py` on staging.

## Decisions (already made; do not revisit)

1. **Drums do NOT get the hook.** Keep it out of the drum preview
   (maintainer's option B). No proven drum velocity word exists (drum adapter's
   stock start carries only a key). The drum preview keeps the `opxy_note`
   export (priming only, never dispatched) so `VOICE_EXPORTS` stays uniform.
2. **PR becomes draft** until the hardware probe confirms the +16 register
   (`gh pr ready --undo 361`); the ABI paragraph comes out until then.
3. **One dispatch owner**: a new SDK header `sdk/include/opxy/voice_note.hpp`.
   Extract first, then rewire every harness onto it (no third copy is created
   at any point: the two existing copies are deleted in the same commit).

## Approach

Work in worktree `/Users/wsz/.tmp/sdk-velocity`. Every step is one conventional
commit (repo style: short lowercase imperative). After each commit the tree
must still pass `python3 sdk.py check` (except steps that intentionally touch
docs only, where a fast re-run is still cheap).

### Step 0 — Sync and rebase

```sh
cd /Users/wsz/.tmp/sdk-velocity
git fetch fork && git reset --hard fork/voice-velocity   # f0e7ce9
git rebase origin/staging                                # 2185ec8
```

Expected conflicts in `docs/abi-1.2.1.md`, `docs/api.md`, `tooling/machines.py`.
Resolution rule: keep staging's content, re-apply the PR's intent on top. In
`tooling/machines.py` do NOT re-add the PR's "1.8 also lets a synth declare
..." line — the history line is rewritten as 1.11 in Step 3. Verify with
`git diff origin/staging..HEAD --stat` (≈20 files, +516/−14 modulo the new edits).

### Step 1 — Commit "Share one note-hook dispatch across the SDK"

New file `sdk/include/opxy/voice_note.hpp`, verbatim (C++11; no definition of
`probe::chosen` is needed — it is only used unevaluated):

```cpp
#pragma once
#include <opxy/types.hpp>
#include <type_traits>
#include <utility>

// The synth note hook's one dispatch (SDK 1.11). An engine may define, in the
// namespace that declares Voice and Shared:
//     void note(Voice& voice, Shared& shared, opxy::u32 velocity);
// called once per note onset, after initialize, before start, with the note's
// raw 1..127 velocity. Argument-dependent lookup finds it, and an exact
// non-template match wins over the fallback; a machine without one is never
// called and behaves as before. The firmware adapter, the browser preview,
// the CPU estimate and the sound-test templates all dispatch through call().
namespace sdk_voice_hook {
// Sees no `note` of its own: an engine's comes only through ADL.
template <class V, class S>
auto viable(int) -> decltype(void(note(std::declval<V&>(), std::declval<S&>(), opxy::u32{})),
                             std::true_type());
template <class V, class S> std::false_type viable(long);
template <class V, class S> constexpr bool declared() {
    return decltype(viable<V, S>(0))::value;
}
}

namespace sdk_voice_note {
// The fallback, returned so the guard below can tell it from an engine's void
// hook. A machine without `note` gets this no-op.
struct Fallback {};
template <class V, class S> Fallback note(V&, S&, opxy::u32) { return Fallback(); }
// An engine's hook must carry exactly the contract signature: a near miss
// (`const Shared&`, an `int` or `u8` velocity, `Voice` by value) would lose to
// the fallback and silently never run, so the guard rejects it at compile time.
namespace probe {
template <class V, class S>
auto chosen() -> decltype(note(std::declval<V&>(), std::declval<S&>(), opxy::u32{}));
}
template <class V, class S> void call(V& voice, S& shared, opxy::u32 velocity) {
    static_assert(!sdk_voice_hook::declared<V, S>() ||
                      std::is_same<decltype(probe::chosen<V, S>()), void>::value,
                  "note must be void note(Voice&, Shared&, opxy::u32), in the namespace that defines Voice");
    note(voice, shared, velocity);
}
}
```

How the guard works (keep this comment knowledge in the file): `viable` says an
engine note exists that is viable for the contract arguments; `probe::chosen`
resolves `note` in a scope that sees BOTH the fallback and the engine's note —
an exact engine note wins (non-template preferred on an exact tie) and returns
`void`; a near-miss loses to the exact fallback and yields `Fallback`. So
`declared() && chosen() != void` ⇒ wrong signature ⇒ compile error.

Edits in the same commit:
- `sdk/platform/opxy-1.2.1/voice.cpp.in`: delete the inline `namespace
  sdk_voice_note { ... }` block (lines ~89–96 at the PR head); add
  `#include <opxy/voice_note.hpp>` beside the other includes; the call sites
  `sdk_voice_note::call(...)` stay as they are.
- `sdk/host/voice_preview.cpp.in`: delete the inline namespace block (lines
  ~19–27); add `#include <opxy/voice_note.hpp>` after `#include "engine.cpp"`;
  replace the unconditional dispatch in `opxy_start` with the placeholder
  `@NOTE_CALL@` on its own indented line.
- `tooling/adapters.py`, `generate_voice_preview`: after `values['DRUM_SELECT']`
  add:
  ```python
  values['NOTE_CALL'] = ('sdk_voice_note::call(*voice, shared, note_velocity[voice - voices]);\n    '
                         if kind == 'synth' else '')
  ```
  (Drum previews never fire the hook; the export list is unchanged.)
- `tooling/cpu.py` SYNTH template: after `#include "engine.cpp"` add
  `#include <opxy/voice_note.hpp>`; change the timed setup from
  `[&] { @NS@::start(voice, shared, 1); }` to
  `[&] { sdk_voice_note::call(voice, shared, 127u); @NS@::start(voice, shared, 1); }`
  so strike setup is estimated. EFFECT template untouched.
- `sdk/templates/synth/tests/sound.cpp.in` and
  `sdk/templates/synth-eight/tests/sound.cpp.in`: after
  `@ID@::initialize(voice, shared, 1);` insert
  `sdk_voice_note::call(voice, shared, 127u);` and add the
  `#include <opxy/voice_note.hpp>` after `#include "../engine.cpp"`.

### Step 2 — Commit "Clamp the note velocity to 1..127"

- `voice.cpp.in`, in `@PREFIX@_start`: replace
  `handle->velocity = static_cast<opxy::u8>(velocity & 0x7fu);` with
  `handle->velocity = static_cast<opxy::u8>(velocity < 1u ? 1u : velocity > 127u ? 127u : velocity);`
  (keep the trailing comment about the waiting note's velocity surviving).
- `voice_preview.cpp.in`, `opxy_note`: same clamp expression replacing
  `velocity & 0x7fu`.
- `sdk/browser/engine.mjs` already quantizes to 1..127 (`Math.max(1, ...)`) — no change.

### Step 3 — Commit "Version the note hook as SDK 1.11"

- `tooling/machines.py`: `SDK_VERSION = (1, 11)`; delete the PR's added
  "1.8 also lets ..." line; append to the history comment block:
  ```
  # 1.11 lets a synth declare an optional note(voice, shared, velocity) hook, dispatched
  # by one SDK header on every harness that starts a voice.
  ```
- `docs/api.md`: the `sdk` field row "This SDK is 1.10 and builds 1.0 to 1.10
  machines" → 1.11 both places; in the hook paragraph add the version marker
  repo style ("(`note`, SDK 1.11)" or the file's prevailing style for version
  markers) and fix the ordering claim (maintainer inline at docs/api.md:286):
  replace "on every begin path, including a same-index restart" with wording
  that says every begin path calls the hook between `initialize` and `start`,
  and that a note restarting on the voice index it already holds reaches the
  hook **without** a fresh `initialize`.
- `docs/preview.md` already says the runner probes `opxy_note` — keep.

### Step 4 — Commit "Test the velocity fixture from its own files"

- `tests/machine_fixtures.py`: `NAMES` back to
  `('probesynth', 'probesources', 'probefx', 'probesample')` — keep the folder
  and its doc-comment line (the fixture stays; only the every-kind collection
  stops paying for it).
- `tests/test_voice_velocity.py`:
  - Delete `preview_compiler()` and the import-time `WASM_CXX`. Add:
    ```python
    from tooling.preview_doctor import probe as preview_probe

    _WASM = {}

    def wasm_compiler():
        """A WASM compiler that builds and runs a module, or None; probed once,
        lazily, through the preview doctor (OPXY_TEST_PREVIEW_CXX, default clang++)."""
        if 'cxx' not in _WASM:
            node = shutil.which('node')
            if node is None:
                _WASM['cxx'] = None
            else:
                compiler = os.environ.get('OPXY_TEST_PREVIEW_CXX', 'clang++')
                try:
                    preview_probe(compiler, node)
                    _WASM['cxx'] = compiler
                except (ValueError, OSError):
                    _WASM['cxx'] = None
        return _WASM['cxx']
    ```
  - `CORE` and `ENGINE` constants: delete. `run_engine` reads them from the
    fixture: `(fixture('probevelocity').root / 'core.h')` and
    `(fixture('probevelocity').root / 'engine.cpp')`; give `run_engine` an
    engine override of `None` meaning "the fixture's engine"
    (`test_velocity_reaches_the_hook` passes `None`; `PLAIN` and `COUNTED`
    stay as test-local strings — they are test-specific variants, not fixture
    duplicates).
  - `test_preview_note_velocity_end_to_end`: drop the
    `@unittest.skipUnless(WASM_CXX, ...)` decorator; first line of the body:
    `compiler = wasm_compiler()` then
    `if compiler is None: self.skipTest('No working preview WASM compiler and Node (set OPXY_TEST_PREVIEW_CXX)')`;
    use `compiler` for the build command.

### Step 5 — Commit "Drop the preview-compiler passthrough from checks.py"

Revert `tooling/checks.py` to staging (remove the 4-line
`OPXY_TEST_PREVIEW_CXX` passthrough added by the PR). The maintainer wants it
in its own PR with its own reasoning; without it, qualification runs use the
`clang++` default, matching the existing preview tests.

### Step 6 — Commit "Install the wasm compilers before the source checks"

`.github/workflows/ci.yml`: move the conditional clang/lld apt install out of
"First machine" into a new step ("C++ and WebAssembly compilers") placed
immediately before the "Source checks" step (`python sdk.py check`), so the
wasm velocity test runs in CI instead of silently skipping. "First machine"
keeps its own guard (now a no-op) and its 5-minute timeout.

### Step 7 — Commit "Keep the unverified +16 register out of the ABI doc"

`docs/abi-1.2.1.md`: delete the paragraph the PR added at ~line 283 (the one
starting "Stock's note-start entry at vtable `+16` passes the note ..."). The
register contract stays documented only in the PR description until the
hardware probe. After hardware confirmation it comes back citing the
observation, not the drum analogy.

### Step 8 — Local verification battery (before push)

From the worktree:

```sh
python3 -m unittest discover -s tests -p test_voice_velocity.py -v
python3 sdk.py check
```

If a wasm compiler exists locally (e.g. `/opt/homebrew/opt/llvm/bin/clang++`;
`brew --prefix llvm` — confirm at run time), also:

```sh
OPXY_TEST_PREVIEW_CXX="$(brew --prefix llvm)/bin/clang++" python3 -m unittest discover -s tests -p test_voice_velocity.py -v
```

New-behavior proofs expected:
- `test_velocity_reaches_the_hook` → `(100, 40)`; `test_without_the_hook...`
  → `(100, 100)`; waiting-note test → `(40, 13, 13, 0)` (hook counts unchanged
  by the refactor).
- Guard proof (throwaway, not committed): compile a tiny engine whose `note`
  takes `(Voice&, const Shared&, int)` against the shared header with the host
  compiler and observe the static_assert message; then compile one with the
  exact signature and observe success. Delete the scratch files.
- `python3 sdk.py check` green in public mode (skips allowed).

### Step 9 — PR mechanics (push and every remote action gated on user validation)

Stop here first: present the commit list (`git log --oneline origin/staging..HEAD`)
and the `git diff origin/staging..HEAD` summary to the user and wait for explicit
approval. Only then:

```sh
git push fork voice-velocity --force-with-lease
gh pr ready --undo 361 --repo etcetc000/open-xy          # back to draft
gh pr edit 361 --repo etcetc000/open-xy --body-file ...   # updated body
gh pr comment 361 --repo etcetc000/open-xy --body ...     # reply to review
```

Body updates: Compatibility — the hook now ships as SDK 1.11 through
`sdk/include/opxy/voice_note.hpp`, dispatched by the firmware adapter, the
synth browser preview, the CPU estimate and the sound-test templates; drums
keep preview/device parity (no hook either side). Add: velocity clamped to
1..127 on device and preview; rebase onto staging (SDK 1.10 tip); checks.py
passthrough moved to its own PR; ABI +16 paragraph withdrawn pending hardware.
Comment: reply to the review listing each item and where it landed.
If the user withholds validation, the local commits stand and nothing remote
changes; report the state and stop.

### Step 10 — Hardware probe prep (blocker 1, user executes device steps)

Create a throwaway probe machine `local/velocity-probe/` (gitignored):
`python3 sdk.py new --id velocityprobe --name "Velocity Probe" --out local/velocity-probe`
then edit `engine.cpp`: `note()` stores `velocity` in the voice; `render`
drives an internal table oscillator whose frequency is a pure function of
velocity (e.g. 110 Hz · 2^(velocity/12)) and ignores stock pitch — pitch must
rise with strike hardness if the captured word is the velocity, and sound
wrong/static otherwise (stock's post-render loudness chain cannot mask pitch).

Build and record evidence:
```sh
python3 sdk.py build --machine local/velocity-probe --out local/velocity-probe-001
```
capture the printed module/asset hashes. Then hand the user the device steps
(`python3 sdk.py device push <folder>`, play keys at different velocities,
listen for pitch tracking velocity) — device operations are user-run per the
repo rules.

If confirmed: re-add the ABI paragraph citing the observation + hashes, then
— after user validation, same gate as Step 9 — post the evidence as a PR
comment and `gh pr ready 361`. If a different word carries
velocity: one-line re-pin in `voice.cpp.in`, rerun the battery, same evidence
flow. If hardware is unavailable: the PR stays draft; say validation remains
pending and a maintainer can measure it.

## Critical files & anchors

- `sdk/include/opxy/voice_note.hpp` (new) — the single dispatch + guard.
- `sdk/platform/opxy-1.2.1/voice.cpp.in` — `@PREFIX@_start` takes
  `(self, note, velocity)` (~line 194); dispatch in `begin` (~line 99) and the
  in-place restart (~line 209); clamp in `_start`.
- `sdk/host/voice_preview.cpp.in` — `note_velocity` array, `opxy_note` (~line
  49), `@NOTE_CALL@` in `opxy_start`.
- `tooling/adapters.py:107-115` — `generate_voice_preview` kind placeholders.
- `tests/test_voice_velocity.py` — rework per Step 4.

## Assumptions & contingencies

- numpy 2.3.5 pinned env assumed present for preview builds locally; if the
  interpreter lacks it, the wasm test is skipped and CI is the authority (PR
  body already words it this way).
- Rebase conflicts are the three known files; anything else → inspect, same
  resolution rule (staging content, PR intent re-applied).
- If `python3 sdk.py check` fails locally on pre-existing environment errors
  (the PR body reported 17 pre-existing errors on a pristine checkout), run
  the same suite on a pristine `origin/staging` worktree to confirm the
  failures are pre-existing and identical; report, don't chase.
- Wurli machine.json must move to `"sdk": "1.11"` when this merges — that edit
  belongs to the op-xy-wurli repo, not this PR; noted in the PR comment only.
