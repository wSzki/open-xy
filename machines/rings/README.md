# Rings

Rings is an experimental fixed-point resonator inspired by Mutable Instruments
Rings. It provides three models: modal resonances, sympathetic strings, and
inharmonic strings. The modal model uses a 60-mode bank; the sympathetic model
couples all eight strings. Inharmonic uses one dispersive string per context,
as in the original monophonic model. This is an algorithmic rewrite, not a
bit-identical port of the original floating-point code. Source and license
notes are in [`vendor/VENDOR.md`](vendor/VENDOR.md).

## Controls

The four main controls have normalized values from 0 to 1:

| Control | Default | Function |
| --- | ---: | --- |
| Structure | 0.25 | Resonator structure |
| Brightness | 0.5 | Excitation and resonator brightness |
| Damping | 0.3 | Resonance damping |
| Position | 0.25 | Excitation position |

Model and Output are alternate controls on the first two dial positions:

- **Model** selects Modal (0), Sympathetic (0.5), or Inharmonic (1); default: Modal.
- **Output** selects Out (0), Mix (0.5), or Aux (1); default: Mix.

Mix combines the left and right outputs at quarter level.
The three factory presets are **Modal**, **Sympathetic**, and **Inharmonic**.

The DSP core runs at 48 kHz and uses 147/160 sample-rate conversion. Its output
framing has a 147-frame delay. Two sounding contexts are shared: starting a
third takes the oldest context. Browser preview has eight lightweight voices,
not eight complete DSP instances.
## Display

The display draws three solid strings: horizontal at Structure 0 and tilting
as Structure rises. Brightness thickens the stroke, Damping shades it from
white toward dark grey, and Position sets the spacing between the strings.
Note strikes vibrate them at 12–30 Hz, and the visible ring-out stays inside
the preview's audible window so the lines stop when the sound stops. Reduced
motion keeps them stable. This is a string visualization, not an exact PCM
waveform.

## Preview

Preview is device-free. Use the repository's existing preview environment and
a fresh output directory:

```sh
python sdk.py preview --engine rings --out local/rings-preview-001
python tools/serve_harness.py --preview local/rings-preview-001
```

Open the harness URL printed by the server, enable audio, and select Rings.
To build firmware with Rings, use the opt-in
[`profiles/collection-rings.json`](../../profiles/collection-rings.json)
recipe; the default collection remains unchanged.

The Rings-only visible monitor ranges from 0 to 4 and defaults to 2.6×, about
12 dB higher than the former 0.65× setting. This monitor gain is isolated from
the DSP, patches and Output selection (Out/Mix/Aux); it adds no DSP boost.


Native/device qualification is not complete; no device behavior or listening
result is claimed here.
