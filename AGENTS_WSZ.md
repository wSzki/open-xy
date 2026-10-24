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
- Keep the base repo clean except for explicitly approved shared changes. Fork conventions live here; logs and indexes in `CHANGELOG.md` / `WORKLOG.md`; generated and research outputs under ignored `local/`.
- `RINGS_ENGINE_PLAN.md` describes the authorized, device-free Rings port and
  collection-selection prerequisite. Target a very close-sounding instrument,
  not bit-identical floating-point behavior. Exact sample errors and modal
  peak identities are diagnostics, not prerequisites to implementing the DSP.
- Preserve the selected three models, full modal/string banks, two independently
  leased contexts and six controls. Prioritize a playable integer port and
  representative reference/port A/B recordings over expanding the validator.
- Follow `machines/macroosc` vendoring conventions: pinned sources, original
  notices, per-file hashes and explicit adaptation notes. Do not inherit its
  sample-rate or RNG-reset policy blindly: Rings retains a 48-kHz core with the
  proven 44.1-kHz converter, and seeds each context only on a new lease.
- Defined integer arithmetic, bounded memory, valid coefficients, callback
  allocation freedom and ownership isolation remain requirements. Do not
  mistake relaxed sonic matching for permission to hide numerical faults.
- Until the user lifts the overnight restriction, do not install dependencies,
  perform destructive cleanup or write outside this checkout, including compiler
  temporaries, caches, browser profiles and generated evidence. Native/firmware
  outputs must remain outside checkouts, so defer those builds rather than
  redirect them into `local/`. Host/preview outputs may use `local/`.
- Every host compile/run, including delegated work, must use the approved raw
  sandbox, an explicit compiler/sysroot and repository-local HOME, TMPDIR and
  caches. A local output path alone does not contain temporary writes. Record
  the outer sandbox invocation with the evidence; do not use bare compiler calls.
- Run `python sdk.py check` for every change; checks must stay active under
  `python -O`.

## Pointers

| Path | What |
|---|---|
| `CHANGELOG.md` | what changed in this fork, newest first |
| `WORKLOG.md` | dated session log (what happened, where it lives) |
| `RINGS_ENGINE_PLAN.md` | current authorized Rings implementation, sound scope and verification approach |
| `local/port-plan-2026-10-24.md` | current audited MI port plan; supersedes the historical `local/port-plan.md` |
| `local/research/outputs/opxy-hardware-mi-research-audit-2026-10-24.md` | current verification verdict, corrected brief and evidence index; earlier corpus remains preserved under `local/research/` |
| `../opxy_jailbreak/` | earlier firmware-access research (outside this checkout) |

## Log conventions

- Provision `WORKLOG.md` and `CHANGELOG.md` at the start of project work if absent; keep both updated throughout implementation.
  Update `WORKLOG.md` at meaningful implementation and verification milestones;
  distinguish implemented, exercised, diagnostic-only and blocked work.
  Keep `CHANGELOG.md` concise, with one bullet per landed change.
- Commit small, coherent, verified milestones frequently. Do not stage another
  worker's live files, publish unverified work as a working engine or push
  without a separate instruction.
- Findings and data go to dated files under ignored `local/`, never into this
  file. Preserve closed evidence; supersede it rather than rewriting it.
