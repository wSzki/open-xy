# Fork notes (wSzki) — open-xy

This fork adds third-party engine ports on top of the upstream OP-XY SDK.
Upstream rules in [AGENTS.md](AGENTS.md) and the docs it names apply unchanged
to all shared code; this file only adds fork-specific conventions and pointers.

## Fork rules

- The dev loop is device-free: `python sdk.py preview`, `sdk.py check` and
  host-side build verification never touch the unit. Device operations
  (flashing, installing, any hardware write) require a separate explicit user
  instruction — never infer them. The unit runs OS 1.2.0 with no documented
  downgrade path; treat it as unrecoverable if bricked.
- Keep the base repo clean: the only fork edit to an upstream file is the
  pointer section at the end of `AGENTS.md`. Fork conventions live here; logs
  and indexes in `CHANGELOG.md` / `WORKLOG.md`; generated and research outputs
  under ignored `local/`.
- Port work follows the vendoring conventions of `machines/macroosc`
  (VENDOR.json SHA pins, `OP-XY:` patch comments, tables regenerated at
  44,100 Hz, `LeasePool` for delay memory, per-voice RNG reseed).
- Run `python sdk.py check` for every change; checks must stay active under
  `python -O`.

## Pointers

| Path | What |
|---|---|
| `CHANGELOG.md` | what changed in this fork, newest first |
| `WORKLOG.md` | dated session log (what happened, where it lives) |
| `local/port-plan.md` | engine port plan: ABI constraints, candidate tiers, MI focus |
| `local/research/` | deep-research corpus: hardware, Blackfin+ architecture, MI code, port precedents, brick-risk; protocol artifacts under `local/research/outputs/` |
| `../opxy_jailbreak/` | earlier firmware-access research (outside this checkout) |

## Log conventions

- `CHANGELOG.md`: one bullet per landed change. `WORKLOG.md`: one entry per
  session. Findings and data go to dated files under `local/research/`, never
  into this file. Supersede with a new dated file; never rewrite closed ones.
