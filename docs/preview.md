# Local preview and patch protocol

Browser previews audition synths: their controls, alternates included, their
sound and their own screen.
Sample engines and effects use stock pages and stock sample or send processing,
so they have no browser preview; they are checked by the native build.

For a new instrument, follow [Your first machine](first-machine.md). Check the
preview toolchain with `python sdk.py doctor --preview`; custom schema-1 sources
build with `python sdk.py preview --source /path/to/machine --out local/custom-001`.
Custom sources always use local authoring checks and cannot qualify a collection.

Build a selected example, then serve its authenticated snapshot:

```sh
python sdk.py preview --engine fm2op --out local/preview-001
python tools/serve_harness.py --preview local/preview-001
```

Use `--cxx /path/to/clang++` if Clang is not on PATH, or `--port 8937` if the
server port is occupied. Open http://127.0.0.1:8917/, enable audio, then play
`A W S E D F T G Y H U J K` or the on-screen keyboard. Escape/Stop all and loss
of window focus release notes. Monitor gain is separate from engine parameters.

## Make shortcuts

Plain `make` (or `make run`) serves the latest already-built
`local/$(ENGINE)-preview-*` snapshot; `ENGINE` defaults to `rings` and `PORT`
to `8917`. It does not build implicitly.

`make preview` creates a fresh WASM preview using the installed toolchain; it
refuses an existing output directory. `make check` runs `sdk.py check`, and
`make help` lists the commands. Override `ENGINE`, `PREVIEW`, `PREVIEW_DIR`,
`PORT`, `PYTHON` or `WASM_CXX` as needed. Preview build checks include the
SDK's ABI, fixed-memory, ownership and drawing checks.


## MIDI input

Enable audio with the existing audio button, then choose **Connect MIDI input**
to request browser permission. Connecting does not enable audio or choose a
device automatically; select a keyboard from the input list. MIDI input supports
note-on and note-off for all 16 channels, with note-on velocity controlling
level. A note-on with velocity zero is treated as note-off.

Web MIDI requires a supported browser and a secure context. `http://127.0.0.1`
is treated as secure for local development; an insecure or unsupported browser
can still use the on-screen and computer keyboards. The browser may show a
permission prompt only after Connect is activated. Chrome's prompt may mention
controlling or reprogramming devices for all MIDI; this preview requests
`{sysex: false}` and remains input-only.

The preview does not send MIDI, use SysEx, or open MIDI outputs. Stop all, audio
mute, window blur, and hidden-page cleanup release held MIDI notes; page exit
disconnects the selected input. No hardware MIDI device qualification is
implied.

To change sound or display, edit the C++, rebuild into a fresh directory,
restart the server and reload. Edit `definition.json` for controls, then
rebuild.
The server rejects stale sources at startup; an already running server keeps
its selected snapshot. It serves a file allowlist, never the repository root.

Every preview build runs shared ABI, fixed-memory, input-ownership,
declared-peak, voice-lifecycle and drawing smoke checks. They do not prove
callback correctness or timing; sound correctness is the machine maintainer's
responsibility.

## Host behavior

The AudioWorklet runs compiled C++ at 44.1 kHz in 64-sample blocks, with eight
preallocated voices, a 5 ms attack and 120 ms release audition gate. Controls
apply at the next kernel block; retrigger discards pending samples immediately.
The preview does not emulate stock envelopes, filters, LFOs or effects, and
cannot measure Blackfin CPU use. No firmware or device connection is required.

## Patch format

[controls.mjs](../sdk/browser/controls.mjs) implements validation.
A browser patch contains:

| Field | Value |
| --- | --- |
| `kind` | `opxy-machine-preview` |
| `machine` | Engine ID |
| `parameters` | Exactly the declared normalized controls, finite and in [0, 1] |
| `view` | `routing` (the machine's screen) or `waveform` (an audio scope) |
| `parametersQ31` | Eight words: ordered rounded Q31 controls followed by zeros |

Import validates the Q31 array against the normalized values. Invalid input leaves the current patch unchanged. The UI
limits patch files to 16 KiB. Exported JSON is a browser patch, not an OP-XY
project; exported SVG is a single frame, not executable display code.

## Local automation

After enabling audio through the UI, `window.machineLab` provides:

| Method | Input/result |
| --- | --- |
| `getPatch()` / `loadPatch(patch)` | Export/import the same validated format |
| `setParameters(values)` | Complete map of normalized controls |
| `noteOn(midi, velocity = 0.8)` / `noteOff(midi)` | MIDI note 0..127; velocity (0, 1] |
| `panic()` | Stop all voices |
| `getStatus()` | Audio state, backend identity, note counts and output RMS |

The SDK-owned AudioWorklet transport accepts `{type: 'parameters', value}`
(Q31 array), `{type: 'on', note, velocity}`, `{type: 'off', note}` and
`{type: 'panic'}`. It reports readiness/layout or an error. Contributors use the
C++ callbacks; they do not implement this transport themselves.

Preview modules expose ABI version 1, compiled state/buffer sizes, static voice
addresses and lifecycle/render exports. The runner rejects imports, invalid
layout and a sample-rate mismatch. The manifest binds the WASM, artwork and
source hashes and carries the definition and `machine.json` description that
the workbench page shows. These are host interfaces, not a device upload protocol.

## Checks

Run `python sdk.py check`. For a real-browser check, start a dedicated Chrome with a local debugging port and run
`node tools/check_harness_browser.mjs`. `OPXY_CHROME_PORT` and
`OPXY_PREVIEW_PORT` select its endpoints. It exercises audio, controls, patch
round trips and layout. Its expectations come from the served definition, so it
runs against any preview; screenshots remain in ignored `local/`.
