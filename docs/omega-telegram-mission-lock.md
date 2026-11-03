# Ω-Telegram — MISSION LOCK (2026-10-25)

> **Mandatory for every human/agent/session working on this fork.** Read `AGENTS.md`, this document, `docs/apo-development-workflow.md`, and `docs/omega-telegram-next-session-checklist.md` BEFORE planning, editing, testing, reporting or delegating. This is a Creator-approved mission definition, not evidence of implementation. Do not silently reinterpret it.

## 1. Identity and non-negotiable goal

Ω-Telegram is **ONE native Telegram Desktop entity** with integrated APΩ information-processing/reflex core and, **only after body acceptance**, an LLM conditional cognition layer. It is NOT a bot, external controller, API wrapper, second Telegram, separate orchestrator or parallel state machine.

- **Body:** original Telegram Desktop C++/Qt/MTProto functionality, sessions, UI, state, events, networking, storage and native execution paths.
- **APΩ:** always-on, **unconditional** information processing and reflexes, meaning independent of a new Creator instruction or LLM output; not permissionless or unconstrained.
- **LLM (future):** **conditional** cognition interpreting Creator goals and directing the **same native body**, using the **same verified state** and native execution/observation paths. Do not integrate LLM during current body acceptance.
- All parts form one operating entity, not independent products.

## 2. Unified nervous system and body semantics

Every relevant native organ/domain must have a source-grounded **sensory path** to APΩ; events and state deltas must be correlated and normalized into one coherent body state with provenance, ordering, time, causal links and confidence/verification. APΩ must process and respond to signals continuously, including latency-sensitive unconditional reflexes without waiting for a slow periodic cognition tick. Actions must use existing Telegram native mechanisms and return observed native/server results to the same state and durable RAW ledger.

**Reflex analogy:** needle prick → sensed pain → automatic hand withdrawal → subsequently analyze cause. Software mapping: native event → APΩ → verified native reflex action → independently observed result → updated state and memory. Reflexes still respect Creator authority, state preconditions and existing invariants.

Native domains include identity/accounts, chats/messages, groups/channels/forums/admin, search, media/files, calls, stories/reactions, UI, notifications, network, storage, business, payments, Stars/Gifts, Mini Apps and TON where actually supported upstream. **An MTProto schema entry is NOT an APΩ-wired or runtime-verified capability.**

## 3. Formal specification (requirements, NOT claims about current code)

Let O={O1,...,On} be native organs, E_t = union_i E_i(t) their observed signals, S_t verified integrated state, A_t executed action and R_t independently observed result:

```text
S_(t+1) = F_APΩ(S_t, E_t, A_t, R_t)
Unconditional: E_t -> APΩ(S_t) -> native reflex A_t -> observed R_t -> S_(t+1)
Conditional (AFTER BODY PASS):
  (CreatorIntent, S_t) -> LLM -> Goal -> SAME native execution
  -> observed R_t -> SAME S_(t+1)
```

Do not replace or edit the established APΩ mathematical algorithms per Telegram feature. Equations above are architecture requirements, not a license to rewrite source.

## 4. Truth, lifecycle and authority

Published system state must equal **evidence-supported observed state**. Action lifecycle: **PLANNED → VERIFIED → SENT → ACKNOWLEDGED → OBSERVED → CLOSED**. Server ACK != GoalSatisfied. A successful function call, schema inventory, compile, test or ACK alone is not a completed native outcome. Track unknown/pending/failure honestly, including process restarts and partial execution.

Preserve Creator-defined authorization, original Telegram session security and privacy, existing APΩ authority/verifier, state and RAW ledger invariants. No secret/session copying, unauthorized outbound messages, unverified side effects, fabricated PASS or destructive Git shortcuts.

## 5. Fixed execution order

**A — Body inventory:** compare official `telegramdesktop/tdesktop` upstream with `NguyenCuong1989/tdesktop` fork; enumerate actual native organs, event sources, state projections, API/controller entrypoints and return paths.

**B — APΩ nervous-system completion:** wire sensing → normalization → coherent state → unconditional reflex → native action → independently observed result → durable memory/recovery through the EXISTING APΩ runtime. Do not build a second runtime or hardcode a special algorithm for each feature.

**C — Body acceptance:** verify each organ and end-to-end body in actual native Telegram with measured coverage, behavior, restart/replay, regression and upstream parity. Require authentic observed effects; document blocked gates.

**D — LLM conditional layer:** only after explicit body acceptance, integrate LLM into the same body/state/native execution mechanism for Creator-directed goals.

## 6. Body acceptance gate

```text
BodyPASS = sensing AND state AND reflex AND execution
           AND observation AND memory AND recovery AND upstream
```

Each term requires a defined test, source/build/runtime lineage and observed evidence. Any missing gate => **BODY UNDER CONSTRUCTION — NOT ACCEPTED**. Never claim PASS from source presence, unit tests, an MTProto method count, server ACK or unverified summary.

## 7. Source-grounded current baseline (not a fresh runtime certification)

Canonical fork: `NguyenCuong1989/tdesktop`; product branch `apo/master`; upstream `telegramdesktop/tdesktop` (`dev`). APΩ source: `Telegram/SourceFiles/apo/`; existing runtime, state bridge, cognitive loop, executor, verifier, authority and behavior ledger. Some native hooks and actions exist, but **whole-body sensing, integrated reflexes, complete native execution coverage and authenticated end-to-end observation are NOT accepted**. LLM is NOT in current phase. Consult actual git status, source, build and runtime at each new session; never promote this snapshot to current evidence.

## 8. Mandatory agent handoff / anti-drift procedure

1. Read `AGENTS.md` → this MISSION LOCK → workflow → next-session checklist → applicable upstream source and current fork code.
2. Record branch/HEAD, remotes, working tree, local changes, current native process/session state and actual evidence before edits. Preserve existing dirty work. Do not rebase, reset, force-push, clean or overwrite without inspecting effects and authorization.
3. Identify the exact organ, sensory path, state transition, reflex, native execution path and observation/ledger evidence the change advances. If no such connection, it is **out of current mission scope**.
4. Prefer existing native Telegram implementations and existing APΩ interfaces. Preserve upstream parity and algorithm invariants.
5. Run appropriate static/tests/native compile/runtime gates. Distinguish NOT RUN / BLOCKED / FAIL / PASS with actual logs, identifiers and results.
6. Update existing docs/checklist/changelog with verified delta, remaining gaps and exact next action. Do not create duplicate canonical plans or arbitrary version labels.
7. Report what changed, what was observed, what is unverified, and where the next agent resumes. Never equate reported intent with accomplished work.

**MISSION LOCK:** Build one Telegram Desktop native body whose organs feed APΩ, whose APΩ performs unconditional information processing/reflexes and native actions, and whose reported state matches verified reality. Only then add LLM as conditional Creator-command cognition within that same entity.
