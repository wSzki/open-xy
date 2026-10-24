# Rings source notes

The algorithmic reference is Mutable Instruments' eurorack repository at the pinned revision recorded in [VENDOR.json](VENDOR.json), with pinned stmlib inputs; the original MIT license is in [LICENSE](LICENSE). The runnable engine is a manual fixed-point rewrite in [`../model.cpp`](../model.cpp), not copied upstream floating-point DSP and not a bit-identical port. It retains three selected models and their full banks, with a 48 kHz core and the implemented 147/160 sample-rate conversion.

[`../tables.cpp`](../tables.cpp) is generated from the pinned local reference resources, stmlib units and Q31 SRC coefficients. Regenerate it from the repository root with the generator's default pinned input and output paths:

```sh
python machines/rings/tools/generate_tables.py
```
