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

## Mission execution contract and evidence checkpoint — 2026-10-19

**Creator-defined invariant:** build the existing official Telegram Desktop native fork into Ω-Telegram, a fully functional Telegram client with APΩ/Telegram Economy mathematical engines and useful code-based autonomous features. No standalone bot, wrapper, replacement desktop, or new orchestrator. ChatGPT coordinates the existing tools; an LLM is optional and is never required for deterministic functions.

**Fixed evaluation chain:** Source → Build → Runtime → Native Telegram Action → Server ACK → Observed State → Ledger Replay → Measured User Value. A passing reference fixture, static guard, successful compile, or CI label is evidence for only its own level, never product completion.

**Observed source and Git progression:**
- Repository: `C:\Users\pc\Documents\GitHub\tdesktop`, branch `apo/master`.
- `4a9aee0ae4`: removed fabricated Executor success, guarded durable ledger and MTProto parser receipt, reduced telemetry exposure, corrected CI labels.
- `02f2d228cb`: placed mandatory Ω verifier inside Executor and required persisted verification before proceeding.
- `b68c0adbdc`: isolated native unread/profile state by active Telegram account.
- Native Credits wiring (Stars + Telegram in-app TON, not an external TON wallet) and a self-contained APΩ integer type were inspected as **working-tree changes**, not a released feature, at this checkpoint.

**Executed evidence on Titan:**
- Official `win.bat skip-release silent` returned exit code 0 with all 35/35 upstream stages skipped from valid prepared state.
- Official native x64 Debug CMake configure with Telegram test API returned exit code 0, producing `tdesktop/out`.
- MSVC 19.44/Qt 5.15.19 `cl /Zs` compiler checks of eight APΩ native `.cpp` modules returned exit code 0.
- Generated native `Telegram.vcxproj` explicitly includes APΩ Ledger, CognitiveLoop and Executor compilation units.
- Reference Python suite: 45 tests passed; static and Ω fixture gates passed. These do not establish native runtime.
- Native Debug compilation started on Titan, with output under `tdesktop/out/omega-native-debug-build.log`; final compiler/linker result was **not yet available at this checkpoint**.
- GitHub native Debug workflow: run `39177160836`; inspect its completed status and artifact before citing it as evidence.

**Native ledger restart/corruption smoke (Titan):** `Telegram/build/apo_ledger_smoke.ps1` compiled and executed native `apo_behavior_ledger.cpp` against Qt5Cored with MSVC 19.44. Four independent process checks PASS: write three events, restart/replay/append fourth, restart/verify four, reject SHA-tampered file without switching to a default ledger. The first corrupt-file run exposed an implicit-path fallback defect, subsequently repaired and reverified. This is a Ledger module test, **not** evidence of network dispatch, binary launch, full crash recovery or E2E.

**Unfinished prerequisites:** native `Telegram.exe` and `Updater.exe` hashes; controlled separate-data-dir launch; functional Telegram UX; real action_id ↔ MTProto request_id ↔ ACK ↔ observed update correlation; ledger restart/corruption tests; trustworthy economy telemetry; autonomous outcomes and measured user value. Preserve worktrees, sessions and credentials. Continue from the first failed prerequisite, not from an invented PASS.
