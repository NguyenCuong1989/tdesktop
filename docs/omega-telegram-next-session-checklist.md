# Ω-Telegram Next Session Checklist

Canonical identity: the forked Telegram Desktop source/runtime is Ω-Telegram itself (an Active Telegram), not Telegram plus an external agent.

## Current verified baseline
- Canonical local repo: `C:\Users\pc\Documents\GitHub\tdesktop`
- Product branch: `apo/master`
- `origin`: `NguyenCuong1989/tdesktop`
- `upstream`: `telegramdesktop/tdesktop` (push disabled)
- APΩ native source exists under `Telegram/SourceFiles/apo/`.
- MTProto request/response hooks exist at source level.
- Native unread projection exists at source level.
- APO candidate state is dynamic rather than fixed S1.
- Action lifecycle types exist: PLANNED → VERIFIED → SENT → ACKNOWLEDGED → OBSERVED → CLOSED.
- Reference Python suite is a reference/spec check only; never call it SYSTEM PASS.

## Gate 0 — Repository hygiene
- [ ] Working tree clean before sync/build.
- [ ] `git fetch upstream dev --prune`.
- [ ] Confirm `apo/master` is based on current `upstream/dev`.
- [ ] `git submodule sync --recursive`.
- [ ] `git submodule update --init --recursive`.
- [ ] `git diff --check upstream/dev...HEAD` = 0 errors.

## Gate 1 — Native Windows toolchain
- [ ] Follow current upstream `docs/building-win.md`; do not substitute unsupported toolchains by guess.
- [ ] Visual Studio toolchain required by current upstream is installed and discoverable.
- [ ] Required Windows SDK is installed.
- [ ] Python version required by upstream build docs is installed.
- [ ] `cmake`, compiler environment and upstream prepare scripts are available.

## Gate 2 — Native compile
- [ ] Run Telegram upstream prepare/configure sequence with legitimate Telegram API credentials supplied locally, never logged or committed.
- [ ] Configure x64 build.
- [ ] Compile Ω-Telegram native binary.
- [ ] Compiler errors = 0.
- [ ] Linker errors = 0.
- [ ] Record commit SHA, build configuration, toolchain version and binary hash.

## Gate 3 — Launch as Ω-Telegram
- [ ] Launch the fork binary, not the Microsoft Store Telegram binary.
- [ ] Use a separate/controlled data directory first; do not modify or copy authentication secrets blindly.
- [ ] Login/session works.
- [ ] Dialog list, chats, groups, channels, bots, media, search, settings and normal Telegram UX still work.
- [ ] Close/restart preserves valid session/state.

## Gate 4 — Real telemetry baseline
Every PASS below must include numbers.
- [ ] Native lifecycle events captured: expected / captured / missing / duplicate.
- [ ] MTProto requests: total, response match rate, errors, orphan request IDs.
- [ ] MTProto latency: p50/p95/p99.
- [ ] Server updates: expected/captured and dropped-event rate.
- [ ] UI events/state transitions: captured count and mismatch count.
- [ ] Unread projection: Telegram native unread vs APΩ state mismatch = 0.
- [ ] Behavior ledger: event count, sequence gaps, duplicate sequences, hash-chain integrity.
- [ ] CPU, RAM, disk I/O, startup time, shutdown flush time.

## Gate 5 — Fix lifecycle truth
Current source must not claim ACKNOWLEDGED/OBSERVED/CLOSED merely by advancing labels.
- [ ] SENT only after a native Telegram operation is actually dispatched.
- [ ] ACKNOWLEDGED only from matching MTProto/server acknowledgement.
- [ ] OBSERVED only after matching Telegram state/update confirms the external result.
- [ ] CLOSED only when expected state equals observed state.
- [ ] Crash/restart reconciles SENT-but-not-OBSERVED actions before retry.

## Gate 6 — Native action integration
- [ ] Replace placeholder `compute.draft_reply` execution path with a real, bounded native Telegram operation or a harmless test action first.
- [ ] Map `action_id ↔ request_id ↔ response/update ↔ state delta`.
- [ ] Preserve `Capability != Authority` invariant.
- [ ] Never expose Telegram OTP, auth keys, session secrets, API hash, bot tokens, wallet seeds/private keys in telemetry or logs.

## Gate 7 — Ω(P) from real Telegram state
- [ ] Define and document the real telemetry-to-`(C,R,E,B)` mapping.
- [ ] Record S0/S1/D0/D1/diff0/diff1/V0/V1/ΔD/ΔV per decision.
- [ ] APO PASS requires metrics, not a label.
- [ ] Compare decision outcome with observed Telegram result and feed that back into state/economy/trust.

## Gate 8 — End-to-end closed loop
- [ ] Real native event → raw telemetry.
- [ ] Telemetry → state reduction.
- [ ] State → computation/decision.
- [ ] Decision → Ω(P) verification.
- [ ] Authority → native Telegram dispatch.
- [ ] Server response/update → observed result.
- [ ] State/UI/memory update.
- [ ] Closed-loop completion rate measured over repeated trials.

## Gate 9 — Operational resilience
- [ ] Long-run soak test with metrics.
- [ ] Network degradation/reconnect test.
- [ ] Crash recovery test.
- [ ] Update/rebase test against new upstream.
- [ ] No loss of Telegram core functionality.

## Gate 10 — Contributor loop
For every generic defect discovered by Ω-Telegram telemetry:
- [ ] Reproduce on clean `upstream/dev`.
- [ ] Capture before metrics.
- [ ] Create a minimal generic fix branch from `upstream/dev` (not from APΩ product history).
- [ ] Add regression test when feasible.
- [ ] Capture after metrics.
- [ ] Keep APΩ-specific code out of the upstream patch.
- [ ] Submit PR upstream with reproduction, root cause, before/after evidence and platform/toolchain details.
- [ ] After upstream merge, resync Ω-Telegram and remeasure telemetry baseline.

## Definition of operational Ω-Telegram
Do not call SYSTEM/PRODUCT PASS until this chain is measured on the fork binary:

`Install/Build → Launch → Login → Full Telegram UX → Observe → Compute → Decide → Ω(P) → Native Execute → Server Acknowledge → Observed State → UI/Memory Update → Persist → Recover`

The controlling benchmark is telemetry, and telemetry must connect fork evolution back to upstream contribution.
