> **Ω-Telegram MISSION LOCK — mandatory before all work:** Read `docs/omega-telegram-mission-lock.md` (from repository root) and follow its fixed A→B→C→D gates. Current body is NOT ACCEPTED. Do not introduce LLM, external controllers, parallel runtime or claim unverified PASS.

# APΩ Telegram Desktop Development Workflow

This fork treats the modified Telegram Desktop source itself as the agent product. The repository must remain a normal, persistent Git repository with a clean upstream relationship; no temporary copy is canonical.

## Canonical repository topology

- Local source: `C:\Users\pc\Documents\GitHub\tdesktop`
- `origin`: `https://github.com/NguyenCuong1989/tdesktop.git` — writable product fork
- `upstream`: `https://github.com/telegramdesktop/tdesktop.git` — official Telegram Desktop, fetch-only locally
- Product branch: `apo/master`
- Upstream integration branch: `dev` tracking `upstream/dev`

## Multi-agent collaboration work envelope

The Creator Mission Lock is the highest-priority project authority for every
coordinator and delegated agent. Collaboration coordinates work inside the one
native repository and existing APΩ runtime; it does not create another runtime,
controller, state machine or canonical plan.

Before delegation, the coordinator must record one bounded work envelope with a
unique id; the exact native organ and A/B/C/D phase; source branch, commit and
dirty-state summary; assigned agent and exclusively owned files; allowed
commands/tools; acceptance checks; and explicit timeout, cost and output caps.
It must map the exact native sensory ingress, state transition, unconditional
reflex, native execution, independently observed result/ledger return, and
upstream parity path it advances. Each mapping names its source, or records
`NOT_APPLICABLE` with the source-grounded reason; a missing mapping is out of
scope.

Every envelope inherits Creator authority and privacy boundaries: no reading
or copying authentication secrets or raw session files, no unauthorized
outbound side effects, no destructive Git, and the existing verifier and ledger
invariants. Controlled native-session tests may observe scoped runtime state
only when explicitly authorized, without extracting session secrets. Its default is
`READ_ONLY`; editing requires named files, and two agents must never edit the
same file concurrently. Delegated agents must not rebase, force-push or create
automatic commits; commits and publication require separate Creator
authorization outside the delegated envelope. This restriction does not
silently execute or revise the separately documented upstream update sequence.

Phase D or any LLM work must never be planned, delegated or accepted unless the
envelope cites a recorded, explicit full `BodyPASS` in which every Mission Lock
term passed. The current body is **NOT ACCEPTED**, so Phase D/LLM delegation is
out of scope.

Evidence must be reported at its actual level: `SOURCE` for inspected source or
diffs, `TEST` for completed tests, `BUILD` for compile/link results, `NATIVE` for
the fork binary executing the path, and `OBSERVED` for an independently observed
native/server postcondition. Every individual check has one outcome: `PASS`,
`FAIL`, `BLOCKED` or `NOT_RUN`, plus a measured acceptance threshold, measured
value, logs and source/build/runtime lineage. An ACK is only acknowledgement; it
must not be conflated with a confirmed observed result. Status must not be
inferred or reported speculatively.

Every agent returns this machine-readable handoff, preserving unknowns instead
of filling them by inference:

```yaml
TASK: { id: "<envelope-id>", agent: "<cli/identity>", objective: "<bounded-objective>", organ: "<organ>", phase: "<A|B|C|D>" }
INPUT: { revision: "<branch@commit>", dirty_state: "<exact-summary>", owned_files: [] }
LIMITS: { timeout_seconds: "<number>", max_cost: "<amount-or-zero>", output_cap: "<tokens-or-bytes>" }
MISSION_MAP:
  native_sensory_ingress: { status: "<ADVANCES|NOT_APPLICABLE>", path: "<exact-path-or-none>", source: "<source>" }
  state_transition: { status: "<ADVANCES|NOT_APPLICABLE>", path: "<exact-path-or-none>", source: "<source>" }
  unconditional_reflex: { status: "<ADVANCES|NOT_APPLICABLE>", path: "<exact-path-or-none>", source: "<source>" }
  native_execution: { status: "<ADVANCES|NOT_APPLICABLE>", path: "<exact-path-or-none>", source: "<source>" }
  observed_result_ledger: { status: "<ADVANCES|NOT_APPLICABLE>", path: "<exact-path-or-none>", source: "<source>" }
  upstream_parity: { status: "<ADVANCES|NOT_APPLICABLE>", path: "<exact-path-or-none>", source: "<source>" }
BODY_GATE:
  status: "NOT_ACCEPTED"
  full_body_pass_record: "NONE"
  required_pass_terms: ["sensing", "state", "reflex", "execution", "observation", "memory", "recovery", "upstream"]
TOOLS:
  mode: "READ_ONLY"
  allowed: []
  inherited_constraints:
    - "CREATOR_AUTHORITY_AND_PRIVACY"
    - "NO_RAW_SESSION_OR_CREDENTIAL_EXTRACTION"
    - "NO_UNAUTHORIZED_OUTBOUND_SIDE_EFFECTS"
    - "NO_DESTRUCTIVE_GIT"
    - "PRESERVE_VERIFIER_AND_LEDGER_INVARIANTS"
CHANGES: { files: [], summary: "<none-or-factual-delta>" }
CHECKS:
  - id: "<check>"
    outcome: "<PASS|FAIL|BLOCKED|NOT_RUN>"
    acceptance_threshold: "<measured-condition>"
    measured_value: "<value-or-NOT_RUN>"
    logs: []
    lineage: { source: "<revision/path>", build: "<id/hash-or-NOT_RUN>", runtime: "<process/profile-or-NOT_RUN>" }
EVIDENCE: [{ level: "<SOURCE|TEST|BUILD|NATIVE|OBSERVED>", paths: [], claim: "<bounded-fact>" }]
UNKNOWN: []
NEXT: []
```

The coordinator must independently inspect the patch, ownership boundary,
revision/dirty state, acceptance results and cited evidence before accepting it.
An agent handoff, ACK or claimed PASS is not coordinator verification.

## Upstream update sequence

```powershell
git status --short --branch
git fetch upstream dev --prune
git switch apo/master
git rebase upstream/dev
git submodule sync --recursive
git submodule update --init --recursive
.\Telegram\build\apo_verify.ps1 -RunReferenceTests
git push --force-with-lease origin apo/master
```

Never use `git reset --hard` or delete local work as an update shortcut. If rebase conflicts, resolve and verify before continuing.

## Verification gates

A change is not considered native-verified merely because the Python reference tests pass. The gates are:

1. Git topology and clean working tree.
2. APΩ branch based on current `upstream/dev`.
3. All submodules at superproject-recorded commits.
4. `git diff --check` passes.
5. Reference APΩ tests pass when available.
6. Official Windows build toolchain is present.
7. Configure and compile Telegram Desktop successfully.
8. Launch the produced binary and verify native runtime behavior.
9. Verify raw-behavior traces for lifecycle, inbound update, outbound request, UI event, decision, APO verification, execution, observed result, and state update.

## Windows native build baseline

Follow `docs/building-win.md` from the same upstream revision. At the current upstream revision this requires Visual Studio 2026, Windows SDK `10.0.26100.0`, and the documented `-vcvars_ver=14.44` toolset initialization. Do not replace this with an unrelated local compiler and call the product verified.

The official flow is:

```text
prepare dependencies
→ configure x64 with product api_id/api_hash
→ generate Telegram.slnx
→ build Telegram
→ launch generated Telegram.exe
→ runtime acceptance tests
```

## Claims discipline

- `pytest` passing in `telegram-unified-ecosystem` proves the reference/spec tests only.
- Source hooks existing proves wiring at source level only.
- Only a successful native build proves compile/link compatibility.
- Only an executed binary with observed traces proves runtime integration.
- Only end-to-end Telegram actions with observed server results prove execution integration.

This distinction must be preserved in reports and release notes.
