> **Ω-Telegram MISSION LOCK — mandatory before all work:** Read `docs/omega-telegram-mission-lock.md` (from repository root) and follow its fixed A→B→C→D gates. Current body is NOT ACCEPTED. Do not introduce LLM, external controllers, parallel runtime or claim unverified PASS.

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
- [ ] For delegated work, apply the `Multi-agent collaboration work envelope` in `docs/apo-development-workflow.md`; reject missing mission mappings, inherited safety constraints, per-check measurements/logs/lineage, or individual outcomes.
- [ ] Never delegate Phase D/LLM work without a recorded explicit full `BodyPASS`; the current body is **NOT ACCEPTED**.
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
- [ ] Before and after any updater or restart, verify the protected fork binary SHA is unchanged, the relaunched process uses that same binary, APΩ runtime/ledger resumes, and no official Telegram update is installed. Any hash mismatch is `BLOCKED/FAIL`; passing this check is native build/restart evidence only, never whole `BodyPASS`.
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

**Historical Creator-defined invariant:** build the existing official Telegram Desktop native fork into Ω-Telegram, a fully functional Telegram client with APΩ/Telegram Economy mathematical engines and useful code-based autonomous features. No standalone bot, wrapper, replacement desktop, or new orchestrator. The historical statement that ChatGPT coordinates existing tools and an LLM is optional is superseded by the Mission Lock: Phase D/LLM work is forbidden until a recorded explicit full `BodyPASS`; the current body is **NOT ACCEPTED**.

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

**Earlier checkpoint limitations (superseded in part below):** native binary, launch and UI evidence were absent at the time. Action ID / MTProto response correlation, recovery, autonomous outcomes, and measurable value still require genuine execution evidence.

### Native product build and interactive UI acceptance — 2026-10-19

Upstream verified directly via Firecrawl at `telegramdesktop/tdesktop` (`README.md`, `docs/building-win.md`, `Telegram/CMakeLists.txt`, `test/README.md`, `intro/intro_start.cpp`, `intro/intro_widget.cpp`, `mtproto/sender.h`, `api/api_sending.cpp`). Official Windows build is Visual Studio 2026 + MSVC 14.44 + Windows SDK 10.0.26100.0; the native executable is `Telegram.exe`. Do not replace the client's own native session, history, MTProto, UI, login, or test harness with a wrapper.

**Observed on the Titan Windows host, with source HEAD `09ac7cf7cf`:**
- Incremental Windows x64 Debug build completed **exit 0** and linked `tdesktop/out/Debug/Telegram.exe`; ordinary product was launched in an isolated `-workdir` and generated a real visible Telegram window and its own `tdata`. It also closed normally; the pre-existing Telegram process was not terminated.
- Reusable native product regression source: `Telegram/build/apo_native_ui_smoke_scenario.cpp`. Uses the upstream Debug-only `Test::Runner`, `Test::CaptureWidget` / widget raster inspection, `Test::Click`, `Intro::Widget`, and `Intro::details::QrWidget`. It does not log in, expose secrets, or send a user message.
- Reproducible command: `powershell -NoProfile -ExecutionPolicy Bypass -File Telegram/build/apo_native_ui_smoke.ps1`. The script overlays upstream's no-op `test/test_scenario.cpp` temporarily, builds `Telegram.exe`, starts it with `-testagent -workdir <disposable>`, requires real `SCENARIO_RESULT: PASS (failures: 0)` plus `TEST_COMPLETE`, restores the original source byte-for-byte and rebuilds the normal product. This is a native product acceptance test, not a standalone fake UI test.
- **Reproduced script run: PASS, exit 0**. Evidence at `tdesktop/out/apo_native_ui_smoke_fixture/test_evidence/test_log.txt`: six stages, full-size nonblank Telegram welcome window, APΩ Runtime running inside the process, zero/unknown account projections before login, actual **Start Messaging** click, and transition to actual **QR login step** with no fabricated authorization.
- The product test binary SHA-256 was `339269298BF7ADC8146287C2E013DDDB4A2878C53C51DD138E60E7C3AF6978C5`. After restoring the no-op test entrypoint and rebuilding, the ordinary native binary SHA-256 is `F67337601786E6F958BBBC6DAC3BC5828DAE480D285E29A7067C0C6791A02500`. Product UI test and restore build both finished with exit 0. Log paths: `out/apo_native_ui_smoke_overlay_build.log` and `out/apo_native_ui_smoke_restored_build.log`.
- **Certified only:** source-in-build, native binary link, startup, initial UI rasterization and one real product navigation action without login. **Not yet certified:** authorized real Telegram session, account chat history/send/receive, APΩ dispatch with action-id↔request-id, MTProto server ACK, observed update, full action crash recovery, native Telegram Economy value.

**Continue at the first unverified product dependency:** test real Telegram authorization and session projection in a separate authorized fixture, then connect the APΩ executor to the existing native Telegram sending APIs without inventing transport or marking SENT/ACK/OBSERVED optimistically. Preserve the user's existing sessions and do not label the untested E2E chain PASS.

### Ω-Telegram branding and authenticated-profile restart — 2026-10-19

**Product invariant unchanged:** Telegram Desktop itself is the client and APΩ is embedded native code. Branding does not introduce another transport, bot, application wrapper or LLM dependency.

**Source changes:**
- Core app name is `Omega Telegram Desktop`; runtime Qt application identity is `OmegaTelegramDesktop`. Main native window titles visibly include `Ω-Telegram` even when a chat is active. The About dialog discloses that this is a Telegram Desktop fork.
- Windows executable version resources identify the independent Omega fork. Windows resource icon is `Telegram/Resources/art/omega_icon256.ico`, generated reproducibly by `Telegram/build/apo_brand_icon.ps1` from the untouched upstream paper-plane icon plus a purple Ω badge. The Qt runtime icon includes the corresponding badge.
- Installer metadata in `Telegram/build/setup.iss` uses a different name, shortcut identity and GUID, matching `core/version.h`. **The installer has not been packaged or installed/tested yet.** The real Debug binary remains `out/Debug/Telegram.exe` to preserve upstream assumptions about the executable filename.
- Windows desktop shortcut `C:\Users\pc\Desktop\Ω-Telegram.lnk` targets the native binary with `-workdir` directed to `out/omega-native-authorized`. The official Telegram UWP installation/shortcut is unaffected.

**Native product results:**
- MSVC x64 Debug build finished exit 0; executable resources read back as `Omega Telegram Desktop - Native AP Omega Fork`, independent publisher and product name.
- Reusable native `apo_native_ui_smoke.ps1` was rerun with two additional assertions for application name and native window title. The actual `Telegram.exe` Debug harness recorded `SCENARIO_RESULT: PASS (failures: 0)` and `TEST_COMPLETE`, including the new branding checks and native Start Messaging → QR login flow. Its cleanup restored the upstream no-op test entrypoint byte-for-byte and relinked the normal product, both builds exit 0.
- Normal restored branded Debug executable SHA-256: `F62EAA6732273A9A54E99D8299945E18F1A85B35F767BBF10985FCF30BA6E4A3`.
- Creator authenticated the separate profile. The branded binary was restarted using the same profile and displayed the post-login Telegram interface with a title ending `• Ω-Telegram`. The original Telegram UWP process remained running. Authentication success beyond this observed native session is not a substitute for actual request/ACK tests.
- Native APΩ behavior Ledger in the branded application's AppData directory was active, with a read-only sample of 1,093 records, contiguous sequence and previous-hash pointers. Its `EXECUTION` and `OBSERVED_RESULT` event labels **do not prove** server execution or E2E.
- Python reference tests: 45/45 PASS. Source syntax tests and reference tests remain lower-level evidence than product-runtime tests.

**Outstanding engineering conditions:** real APΩ native action dispatch, correlated MTProto ACK, observed post-action Telegram state, durable per-action recovery, and measured autonomous user value. Current default Ledger location follows `QStandardPaths::AppDataLocation` (application-name scope), not the explicit `-workdir` profile; multiple isolated profile instances may therefore share an APΩ ledger. Verify and resolve this boundary before accepting multi-profile or full crash-recovery claims. Keep sensitive Telegram credentials and raw chat data out of build logs, Git and artifacts.

### Native APΩ StateBridge, Ω(P) and unread-priority product verification — 2026-10-19

**Scope:** existing Telegram Desktop C++/Qt/MTProto client; no external agent or transport. Retain full Telegram capabilities and Creator authority.

**Implemented in source (subject to ongoing feature expansion):**
- `apo_runtime.cpp`: profile-local Ledger at `cWorkingDir()/apo/raw_behavior.jsonl`; old shared AppData Ledger remains untouched, not merged into possibly unrelated profile histories.
- `apo_state_bridge.cpp`: persist unread reduction only on true change and reject negative counts; no repetitive identical unread events.
- `apo_verifier.cpp/h`: the sealed Ω(P) formula/thresholds are unchanged; both baseline and candidate must be finite and within `[0,1]^4`. Native fixture tests accept `S1=(0,0.05,1,0.99)` and reject NaN/Infinity/out-of-domain.
- `apo_cognitive_loop.cpp/h`: read the **existing native** `Data::Session.chatsList()->indexed()->all()`, not a fictitious recipient or the full-account badge as a target. Observe at most 512 loaded chat rows, distinguish unknown unread state, rank known chats by mentions → unmuted unread → total unread → list recency, emit up to five anonymized priorities. Retain loaded-row and coverage metadata. Suppress identical ranking observations. Do not submit the previous empty `compute.draft_reply` intent every 10 seconds.
- `apo_behavior_audit.ps1`: inspect structural telemetry only, per profile, including ledger continuity, Telegram base MTProto request/response and native unread ranking. Telegram network RPC callbacks are **not** APΩ action ACK.

**Native build and tested runtime evidence:**
- Actual MSVC x64 Debug `Telegram.exe` linked successfully with exit 0 after one compile fix for upstream `Dialogs::List` (const iterable, no `operator[]`). A separate LNK1104 attempt was caused by a second Omega native profile keeping the target binary mapped; it was shut down using Telegram's official `-quit`, without deleting either `tdata` or stopping official Telegram UWP.
- Product profile was restarted and native `apo.cognitive.unread_ranking` event observed: 247 total loaded chat rows, 247 inspected, 246 known unread states, 80 ranked unread chats, fully covered **loaded list only**. Top five *anonymized* unread message counts: 1397, 868, 454, 384, 369. This does not claim to have crawled all Telegram history or identified business opportunities.
- Source-linked product binary after ranking build SHA-256: `6915C8CE914960FD22B0A8ADD94B4BA768553AC55F8F10C722846EC564E3E131`.
- Native regression rerun with upstream Telegram `Test::Runner`: `SCENARIO_RESULT: PASS (failures: 0)`, `TEST_COMPLETE`. This includes application branding, ledger profile isolation, Ω(P) math/domain validation, deduplicated unread reduction, honest blocked native dispatch, and anonymous cognitive cycle with zero invented actions. Overlay test binary SHA-256 `E1A3C9967B2A691A34FF2871D17CABCB458F5354D94867DB92ED240DE2250D6A`. Upstream `test_scenario.cpp` was restored byte-for-byte and **normal branded binary relinked** successfully, SHA-256 `BF168ADE0DBA7170839160315B3F8C2F25B75E7F3C8FD49B504902EBECB63055`.
- Structural ledger integrity has previously shown no parse errors, sequence discontinuities or broken previous-hash pointers. Old `compute.draft_reply` candidates were rejected at the sealed Ω(P) gate (the saturated total unread was never a specific reply target). Inspect new process events for zero invented submissions.
- Reference tests/CI are a separate lower-level gate; no assertion of full Telegram functional regression or APΩ native side-effect E2E is made.

**Still open:** a truly authorized bounded native operation with `action_id → native request_id → server response → postcondition`; persisted pending/recovery state and unknown-outcome reconciliation; side-effect deduplication; Creator goal/authority UI; full Telegram upstream regression. Do not label a native read or network request as success solely because of a completion callback. Core Ω(P) formulas and Economy's six sealed equations must not be altered to get PASS.

### Authenticated native read-only action and behavioral output — 2026-10-19, 12:11 ICT

**Unchanged mission:** complete Telegram Desktop client + native APΩ mathematical/cognitive execution; Telegram upstream functionality remains the sensory and action surface. Keep the Creator's authenticated profile running without any unsanctioned user-visible send, delete, join, or payment.

**Source linkage:** existing `apo_cognitive_loop.cpp` plans at most one `dialogs.inspect_server` operation per account per process after a fully loaded and known local dialog-list observation. The `C` cost is an explicitly marked bounded-query-budget **estimate**, `E` measures known-row coverage, `R` and `B` are **sealed baseline priors**, not measured risk/authority. `apo_authority.cpp` whitelists this read-only action. `apo_executor.cpp` uses native `session->api().request(MTPmessages_GetDialogs(...))`, records the action/request IDs, persists dispatch and response, and requires a server dialog/local list match before `STATE_CONFIRMED` and `CLOSED`. All other unwired action types remain blocked; no fake success.

**Independent evidence from running authenticated profile:**
- Process `Telegram.exe` PID `327400` with native window title ending `Ω-Telegram`, distinct from official Telegram UWP. Running source-linked Debug executable had SHA-256 `BB0737B62F1FFA9434AE874BA43BC1F91D10E07F2ABCD09CFAFF167730A4DC47`.
- Profile path `out/omega-native-authorized`; confidential `tdata` remains intact. APΩ profile-local Ledger file is `out/omega-native-authorized/apo/raw_behavior.jsonl`; inspect via `Telegram/build/apo_behavior_audit.ps1`, which prints structural aggregates only.
- At 12:11 ICT: **12,820 Ledger rows, parse/sequence/prev-hash errors 0**. `apo.cognitive.unread_ranking` recorded actual native chat-list observation (247 inspected, 246 known, 80 unread-ranked, loaded-list coverage true), and current runtime unread consecutive duplicates 0.
- **Two bounded native read-only operations** in the profile Ledger yielded `SERVER_RESPONSE_RECEIVED=2`, `NATIVE_READ_POSTCONDITION_MATCHED=2`, `SUCCESS_READ_ONLY=2`. Native APΩ action phases include server responses and closure, unlike unrelated baseline Telegram RPC telemetry. This only verifies a **read** operation; it does not prove a side-effect, customer success, recovery after restart, or whole-product completion.
- Previous candidate submissions `REJECTED_BY_APO_VERIFIER=280` remain in the **historical Ledger** and must not be mislabeled as current-process regressions. Current-process rankings emit measured observations rather than speculative `compute.draft_reply` to `unread_inbox`.
- Native regression `apo_native_ui_smoke.ps1` at 11:55: `SCENARIO_RESULT: PASS (failures: 0)`, `TEST_COMPLETE`, including anonymous cognitive non-dispatch and explicit unauthenticated native inspection refusal. Runner restored original upstream `test_scenario.cpp`, relinked normal executable and did not touch the Creator's signed-in `tdata`.

**Remaining product gates:** demonstrate an authorized **state-changing** Telegram action by connecting the native send/edit/etc. subsystem with stable idempotency/random ID, per-action server acknowledgment and actual Telegram update, durable unknown-outcome reconciliation and crash restart without duplicate side effects, then long-horizon Creator goals and measurable Telegram Economy outcomes. Do not use the sealed Ω(P) or economy equations as a substitute for measuring real input and postcondition states. Never close the Creator's existing account process merely to run a test.
