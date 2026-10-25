# Ω-Telegram: Full MTProto Surface and APΩ Native Action Coverage

Source of truth: Telegram/SourceFiles/mtproto/scheme/api.tl, source SHA-256 accad63235e1334bb4ff2f4a1de5c30ef4c46582ba9dfb2db413277b6814ee9b, fork HEAD at generation 379329222419f62398d35f752fe1a54d08428d20.

The companion static capability index, omega-telegram-mtproto-capability-index.json, enumerates all **850 Telegram TL methods**, **1,704 type constructors**, and the **185 update-type constructors** that may provide incoming evidence. Each entry also preserves its full TL signature and typed input fields, including optional flags and generic declarations. Each method has its source line number, TL ID, declared return type, functional domain, conservative operation hint, and separate APΩ wiring status.

**Not a new subsystem:** the index is a read-only, source-grounded inventory. It grants no runtime capability, account permission, Creator authority, or autonomy.

## Closed-loop product boundary

    USER / TELEGRAM UPDATE / TIMER / EXTERNAL SIGNAL
      -> Existing Telegram native UI and C++ runtime
      -> Raw protocol / session / media / call / payment event
      -> StateBridge and per-account Self with evidence lineage
      -> APΩ mathematical computation / Economy / goal-context assessment
      -> Fusion: choose actually available capability
      -> Decision -> Omega(P) -> effective permissions -> Creator authority
      -> Native ActionExecutor -> Telegram MTProto + media / calls / Stars / TON
      -> Correlated server response -> independent observed native postcondition
      -> Durable ledger -> state and memory -> native UI -> next cycle

Formally: effective action set = declared schema INTERSECT implemented native INTERSECT session available INTERSECT effective peer permissions INTERSECT Creator authority INTERSECT satisfied prerequisites and budget.

## Full schema namespace coverage

| TL namespace | Method count | Functional domain |
| --- | ---: | --- |
| messages | 259 | messages_and_search |
| account | 128 | account_and_privacy |
| payments | 72 | stars_gifts_payments |
| channels | 58 | groups_channels_topics |
| phone | 43 | voice_video_calls |
| bots | 40 | bots_mini_apps |
| stories | 33 | stories |
| auth | 30 | authentication |
| contacts | 28 | contacts_relationships |
| help | 25 | platform |
| wallet | 25 | ton_wallet |
| _global | 11 | transport_core |
| chatlists | 11 | folders_and_dialogs |
| stickers | 11 | stickers |
| communities | 9 | communities |
| ephemeral | 8 | ephemeral_messages |
| stats | 8 | stats |
| upload | 8 | media_files |
| aicompose | 7 | native_compose |
| smsjobs | 7 | account_services |
| users | 7 | users |
| langpack | 5 | localization |
| photos | 5 | profile_media |
| premium | 5 | subscriptions |
| updates | 3 | sync_updates |
| toncenter | 2 | ton_network |
| folders | 1 | folders_and_dialogs |
| fragment | 1 | fragment |

**Total: 850 methods in 28 namespaces.** Lexical hints: 337 potential reads, 388 potential mutations, 125 requiring explicit semantic review. These are **hints**, not safe/unsafe determinations. The companion JSON itemizes every method, not just examples.

## Current source-to-runtime evidence

- **Implemented AP Omega read binding:** messages.getDialogs via apo_executor.cpp and existing session->api().request(MTPmessages_GetDialogs(...)). The live native Ledger has two SUCCESS_READ_ONLY results, with action IDs, request IDs, server response, bounded local dialog match, and persisted CLOSED.
- **Limited postcondition:** matching at least one server dialog with a loaded local projection is bounded read evidence; it does not mean all dialogs are synchronized, and says nothing about write actions or crash recovery.
- **Inputs:** apo_state_bridge.cpp mirrors active account, unread aggregates and Telegram Stars/TON credit observations. Per-update normalization is unproven for the full set of 185 update types.
- **Computations:** apo_cognitive_loop.cpp computes attention ordering from loaded Data::Session dialogs; it does not yet interpret arbitrary customer requests or create validated response texts.
- **Policy:** apo_authority.cpp contains selected policy names. An allowed name is not an executable native method.
- **Coverage accounting:** 850 remains the static MTProto schema inventory. The current working tree contains explicit APΩ native bindings for `messages.getDialogs` and `messages.sendMessage` in `Telegram/SourceFiles/apo/apo_executor.cpp`, so the earlier blanket "849 remaining" statement is stale as a source-wiring count. No residual runtime-PASS count is inferred by subtraction: upstream implementation, APΩ source wiring and runtime verification are different sets.
- **Runtime boundary:** only `messages.getDialogs` has the bounded read evidence already documented above. The local uncommitted `messages.send_text` → `messages.sendMessage` path is source-level wiring only; it was not executed in this audit and is not runtime PASS.

### OMEGA-NATIVE-PTS-GATES snapshot — 2026-10-25

- **SOURCE_FACT:** After `PtsWaiter` accepts a direct update or drains an accepted queued update and its native handler returns, `data_pts_waiter.cpp` emits `PTS_HANDLER_RETURNED`. The hook carries the account from its owning `Api::Updates` session. This proves a source-level handler-return boundary only; it is not Telegram `MODEL_STATE_APPLIED` or an independently `OBSERVED` postcondition.
- **BUILD_EVIDENCE:** The recent Windows Debug native compile/link passed for the patched binary with SHA-256 `797C09D6791637378234BFB9E6B5F2C8DF55252E1E63AC20E892A9C13C58BB42`.
- **RUNTIME_EVIDENCE:** An isolated unauthenticated profile produced 72 structurally valid ledger events. A crash/restart run resumed from 37 to 57 events. It produced no `PTS_HANDLER_RETURNED`, as no authorized account was present to exercise that path.
- **PRE-FIX REVIEW (historical):** Runtime PTS behavior was `NOT_RUN` / `NOT_ACCEPTED`; independent reviewer rejected the original queued iterator handling. Direct, queued, duplicate, gapped and PTS-drain runtime tests remained open.
- **POST-FIX SOURCE/BUILD (2026-10-25):** `Telegram/SourceFiles/data/data_pts_waiter.cpp` now takes one ordered queued entry at a time and copies/erases its payload *before* calling the native handler; `clearSkippedUpdates()` no longer resets `_applySkippedLevel` during nested callbacks. Independent Copilot review rated reentrant clearing, source-level ordering and handler-return placement **PASS at SOURCE**, with a **WARN** for lack of direct/queued PTS unit/native evidence. Windows Debug MSVC build/link exited `0`; staged binary size **539,725,824 bytes**, SHA-256 `D62FDE97A223A0676420DA0C0B39693D682AC5CBFA3E4BBD87BECB4151434D2D`.
- **POST-FIX ISOLATED NATIVE:** Fresh unauthenticated `isolated_pts_safety_20261025` launched the exact staged fork; forced kill of only the test PID and restart preserved the binary SHA and continued RAW ledger from sequence **31** to **72**, followed by graceful shutdown. Final audit exited `0` over **77 events**, with zero parse/sequence/hash/link errors. `PTS_HANDLER_RETURNED` count remained **0** because no authenticated session exercised the path; direct/queued/duplicate/gapped/account-switch PTS behavior remains **NOT_RUN**, not `OBSERVED` or `BodyPASS`. The two preexisting Telegram processes remained untouched.
- **ACTUAL-SOURCE UNIT SMOKE (2026-10-25):** `Telegram/build/apo_pts_waiter_smoke.ps1` builds the **actual** current `Telegram/SourceFiles/data/data_pts_waiter.cpp` (SHA-256 `FDFB5E9B98A2BAE686EDFD3E07979688F3E3A19707DF8374325BD2795B48A33D`) with the **real** Telegram `base::flat_map` implementation, while mocking only external `Api::Updates`/session/APΩ/MTP data carriers. MSVC C++20 build and **7/7 deterministic assertions PASS, process exit 0**: direct PTS, queued order, duplicate suppression, unresolved gap timer, callback-driven `init()` and `setRequesting()` cancellation, and exactly one post-handler marker per direct/queued apply. Binary under `out/apo_pts_waiter_smoke/`, SHA-256 `CB143FA9007FDA6509E38AF0DD98D94A412B39682ABEEA77EDFB00EFC3DF0B82`. Independent Copilot review: `ACCEPT_SOURCE_TEST`. This is **BUILD+TEST of actual isolated PTS control flow**, NOT real `Api::Updates`/session/native runtime/server `OBSERVED`. Timer expiry, count=0/short-poll and native authenticated PTS gates remain open. Test files currently belong to the local working tree, not committed into the canonical source history.

- **PTS REENTRANT ARRIVAL COUNTEREXAMPLE — SOURCE TEST ONLY:** An additional optional test in the existing `apo_pts_waiter_smoke` fixture injects pts14 from the pts11 handler while pts12/13 remain queued. Actual compiled `PtsWaiter` yielded apply entry order `11,14,12,13`, `current()=13`, and handler-return telemetry `14,11,12,13`; the probe intentionally exits `1` to preserve the behavioral hazard. The 11 ordinary source regression tests still PASS (100 seeded shuffles, 3,200 callbacks). Upstream `_applySkippedLevel` short-circuit is implicated, but native triggering/reachability is **NOT_RUN**; do not assume a proven on-server fault or silently alter upstream/native/APΩ algorithms. Reproduce through a real native callback path before considering any behavior change.

### Native model-notification sensing bridge — 2026-10-25

- **SOURCE_FACT:** `Telegram/SourceFiles/data/data_changes.cpp::Changes::sendNotifications()` now calls `apo::HookNativeChangeNotificationsFlushed()` after native Peer, History, Message, Entry, Topic, Sublist and Story batched notification streams finish. Existing `apo_runtime.cpp/.h` records a redacted `MODEL_NOTIFICATIONS_FLUSHED` marker (async ledger admission, **not yet durable**) and coalesces a wake in the existing `CognitiveLoop`. The hook first checks runtime liveness, valid source identity, current `StateBridge` account, `Main::Domain::started()`/nonempty accounts and matching active session. This is **notification delivery**, not a verified model delta or remote postcondition. Immediate/realtime notifications remain outside this batch marker.
- **BUILD_EVIDENCE:** Windows Debug MSVC compile/link exit `0` after the domain-empty guard; isolated fork SHA-256 `E6A206D2E8A2B2F9647A3B973C4C61030C2649D706117A9D7FCF16E2A6242C09` (539,732,480 bytes).
- **RUNTIME_BOUNDARY:** Fresh unauthenticated `isolated_model_guard_20261025` launched fork, was force-stopped only for the test and relaunched, then exited gracefully. Ledger audit exit `0` over **25 events**, no parse/sequence/hash/chain errors, sequence continued **8→17→25**, two APΩ starts; SHA unchanged; original two Telegram processes untouched. No account-scoped model notifications could occur without a logged-in session, so native model-flush behavior, cross-account/logout and burst admission tests remain **NOT_RUN**. This is not whole-body `BodyPASS`.

- **SEVEN CATEGORY BATCH COUNTERS (2026-10-25):** `Data::Changes::Manager::sendNotifications()` returns the actual coalesced number of batched notifications before `base::take(_updates)` for Peer, History, Message, Entry, Topic, Sublist and Story, without changing emission order. `Data::Changes::sendNotifications()` captures its account ID **before invoking any subscriber**, skips empty seven-count flushes and sends a fixed `NativeChangeNotificationCounts` struct into the existing APΩ runtime. The redacted ledger marker records these seven integer counts and a SHA-256 account/workdir scope only; it does not record content, IDs or flags, nor establish `MODEL_STATE_APPLIED`. Independent Copilot source audit: `ACCEPT_SOURCE`; a second targeted review requested the pre-callback account capture, which has been applied. It does **not** prove safe deletion of `Data::Changes` during upstream synchronous notification callbacks.
- **COUNTERS BUILD/NATIVE BOUNDARY:** The initial Codex sandbox reported WinGet CMake access failures (`MSB3075`/`MSB4019`), **not** a source compilation failure; coordinator-built native Telegram with MSVC on Titan exited `0` after the lifetime fix. Final staged fork SHA-256 `FFB97948BF43B8F43501138013C524AC3E91696E4A23AED127FFD108CC2A60A7`. A distinct unauthenticated `isolated_category_counts_20261025` profile preserved binary SHA and RAW ledger continuity across a forced termination of **only** its test PID and relaunch: sequence **8→23→25**, two APΩ starts, audit exit `0`, **25** verified events, zero parse/sequence/hash/link errors; both existing Telegram processes untouched. Actual nonzero category counts, reentrant logout, cross-account attribution, sustained-burst telemetry and independent postconditions are **NOT_RUN**, not NATIVE/OBSERVED PASS. Post-build `apo_pts_waiter_smoke.ps1` (7/7), `apo_ledger_smoke.ps1` and `apo_behavior_audit_test.ps1` all exited `0`.

### Protected native binary continuity — 2026-10-25

- **SOURCE_FACT:** `Telegram/SourceFiles/core/launcher.cpp` calls the existing `SetUpdaterDisabledAtStartup()` at the start of `Launcher::workingFolderReady()`, before updater-related startup computation. This establishes the fork-specific source guard; source inspection alone does not prove runtime continuity.
- **RUNTIME_EVIDENCE:** Before the guard, the stock updater replaced `out/apo_body_stage_20261025/Telegram.exe` (fork build: 539,704,832 bytes, SHA-256 prefix `1BC8512A`) with official 7.3.3 (250,053,160 bytes, SHA-256 prefix `CEE6F5`), logged `Update succeed!`, and the isolated unauthenticated ledger stopped after 47 events. After the protected Debug build/link passed, binary SHA-256 `434B37DB9D7B8E1F1582E8F9ABB2FF457D94B0F6F93EC96166E93D48208CF1C1` remained unchanged across forced termination of only the test PID and relaunch of `isolated_guarded_profile`; two AP OMEGA runtime-start events were present and ledger sequence continued 38→64+.
- **VERIFICATION_BOUNDARY:** Profile audit exited 0 over 74 events with zero parse, sequence, hash or chain errors, 17 MTProto requests and 9 correlated responses. Four pending requests are unresolved, not automatically defects. This is native build/restart and ledger-continuity evidence; authenticated native sends, full Body coverage and whole `BodyPASS` remain **NOT ACCEPTED**.

### Source-level Native Body bridge coverage

This is a source snapshot governed by the **[Ω-Telegram MISSION LOCK](omega-telegram-mission-lock.md)**. `Upstream capability` means Telegram Desktop already supplies the native organ or MTProto mechanism. `APΩ-wired` means fork source connects that mechanism to the existing APΩ runtime. `Runtime-PASS` requires observed native execution and cannot be inferred from either source category.

| Native Body bridge requirement | Upstream Telegram capability | APΩ-wired source coverage | Runtime-PASS boundary |
| --- | --- | --- | --- |
| Sensory ingress | Native MTProto dispatch/parser, update application, session state and UI navigation exist in `Telegram/SourceFiles/mtproto/mtp_instance.cpp`, `Telegram/SourceFiles/api/api_updates.cpp`, `Telegram/SourceFiles/data/data_session.cpp` and `Telegram/SourceFiles/window/window_session_controller.cpp`. | `apo::HookMtprotoRequest`, `apo::HookMtprotoResponse`, `apo::HookServerUpdate`, UI history navigation, active-account identity, unread and Credits feed `Telegram/SourceFiles/apo/apo_runtime.cpp` and `Telegram/SourceFiles/apo/apo_state_bridge.cpp`; bootstrap is in `Telegram/SourceFiles/core/application.cpp`. | No whole-body ingress PASS. The hooks are coarse/redacted, per-update normalization across 185 update constructors is unverified, and most native organs have no measured source-to-state coverage here. |
| Integrated state | `Data::Session`, chat-list rows, peer/history types, unread state and account Credits are existing native state. | `Telegram/SourceFiles/apo/apo_state_bridge.h`, `Telegram/SourceFiles/apo/apo_state_bridge.cpp` and `Telegram/SourceFiles/apo/apo_cognitive_loop.cpp` assemble an account-bound `SelfState` plus a bounded `NativeBodyProjection` from loaded dialog rows and persist it before use. | No coherent whole-body state PASS. Current projection covers selected account/dialog/unread/Credits fields, not every organ, and this audit ran no native measurement. |
| Unconditional reflex | Telegram timers, session requests and native state are available without an external controller. | `Telegram/SourceFiles/apo/apo_runtime.cpp` starts the existing periodic `CognitiveLoop`; `Telegram/SourceFiles/apo/apo_cognitive_loop.cpp` can independently propose bounded `dialogs.inspect_server`, then routes it through `Telegram/SourceFiles/apo/apo_verifier.cpp`, `Telegram/SourceFiles/apo/apo_authority.cpp` and the executor without an LLM or a new Creator instruction. | This is a limited periodic, state-conditioned source path, not verified latency-sensitive event → reflex coverage across native organs. No BodyPASS is claimed. |
| Native action | Typed `messages.getDialogs`, `messages.sendMessage` and `messages.getHistory` declarations exist in `Telegram/SourceFiles/mtproto/scheme/api.tl`, and the native session sender applies their typed results. | `Telegram/SourceFiles/apo/apo_executor.cpp` binds `dialogs.inspect_server` to `MTPmessages_GetDialogs`; the dirty working tree also binds approval-gated `messages.send_text` to `MTPmessages_SendMessage` with `random_id` and bounded history readback. | Prior bounded read evidence exists only for `messages.getDialogs`. The uncommitted send path is not build-, session-, ACK-, update-, readback-, restart- or duplicate-side-effect verified and is not runtime PASS. |
| Independent observation | Telegram decodes server results and projects dialogs/messages into native `Data::Session` state. | `Telegram/SourceFiles/apo/apo_executor.cpp` requires server-dialog/local-list matching for the read path; the local send path attempts `random_id` → message ID correlation, native outgoing-text projection, then `messages.getHistory` readback before closing. Global request/response/update observations enter through `Telegram/SourceFiles/mtproto/mtp_instance.cpp` and `Telegram/SourceFiles/api/api_updates.cpp`. | A response callback or ACK alone is not observation PASS. Only the previously documented read postcondition is evidenced; the send postcondition path has no verified runtime evidence. |
| Durable ledger and recovery state | Telegram supplies the profile `-workdir` boundary and lifecycle in the native process. | `Telegram/SourceFiles/apo/apo_runtime.cpp` opens `apo/raw_behavior.jsonl`; `Telegram/SourceFiles/apo/apo_behavior_ledger.h` and `Telegram/SourceFiles/apo/apo_behavior_ledger.cpp` provide hash-chained persistence plus unresolved read/write indexes; `Telegram/SourceFiles/apo/apo_cognitive_loop.cpp` surfaces unresolved outcomes without automatically retrying writes. | Source and historical module/read evidence do not establish full action crash/restart reconciliation. The current dirty ledger/recovery changes were not built or run in this audit. |
| Missing verified runtime coverage | Upstream retains far more native UI, media, calls, stories, groups/channels, business, payment, Mini App, storage and network capability than the MTProto inventory alone describes. | No source claim upgrades unwired upstream capability to APΩ coverage. The explicit executor bindings above are only a narrow bridge subset. | Missing: measured per-organ sensing/state/reflex/action/observation coverage, authenticated state-changing send evidence, durable unknown-outcome reconciliation after restart, duplicate-side-effect prevention, full upstream regression and whole-body acceptance. |

## Verifiable action object

Every execution needs native method, active session and peer rights, source evidence, state freshness, goal linkage, mathematical candidate and Omega(P), Creator authority, cost/risk provenance, action ID, request ID or idempotency reference, dispatch record, server response, independently observed postcondition, ledger lineage, and recovery policy.

Required lifecycle: OBSERVED -> PLANNED -> VERIFIED -> AUTHORIZED -> DISPATCHED -> ACKNOWLEDGED -> STATE_CONFIRMED -> CLOSED. A timeout after DISPATCHED yields OUTCOME_UNKNOWN, never a blanket permission to resend a write. Labels alone never satisfy acceptance.

## Next work, reusing existing source

Expand typed input observations and effective-capability validation using the existing Telegram update/session handlers. Bind prioritized read and write actions to native methods through the existing AP Omega executor, not a wrapper. Prove source -> build -> real session -> action -> server response -> postcondition -> ledger -> crash/restart reconciliation while preserving Telegram Desktop upstream behavior and Creator authority.


## Features that are not enumerated by the TL file

The 850 method count is **only the MTProto API method surface**, not the complete Telegram Desktop feature count. Native sensory/motor interfaces also come from the existing C++ application: window, dialogs, history, media, calls, platform, storage, data, ui, and api. Local file access, notifications, app configuration, media processing, WebView/Mini Apps, session persistence, and UI settings can be local actions without any corresponding new TL method. External TON and other resource adapters likewise require their own typed evidence and authority.

Mapping examples that reuse existing Telegram code:

| Signal or observed input | Native observation source | Computation / decision target | Native action family | Independent result evidence |
| --- | --- | --- | --- | --- |
| New message, edit, read, mention | update* constructors + session/data/history | Context state, goal relevance, priority, Omega(P) | messages.sendMessage, editMessage, deleteMessages (when wired/authorized) | Server reply and resulting history item/update |
| Chat/group/channel change | messages/channels/communities + dialog model | Rights, relationship graph, event priority | channels.* and messages.* | Membership/rights projection from Telegram |
| Timer or backlog | Qt timer + durable ledger + current session | Pending goal, deadline, retry eligibility, optimization | Existing supported Telegram methods | Observed postcondition; unknown if timeout |
| Stars, gift, payment | payments.* + native credits | Revenue, cost, capital, trust and policy | payments.* within Creator limits | Payment/transaction status, not a UI callback |
| Calls, media, documents | phone.*, upload.*, local media modules | Task context, cost and permission | Existing Telegram calls/media subsystems | Call/media state confirmation |
| Creator UI intent | Native Qt actions, per-account Self | Goal, authority changes, scheduling | AP Omega native action gate | Updated goals/policy and durable audit |
| External TON or Mini App | Native WebView and external adapter when implemented | Economy formula, source credibility, available funds | Authorized wallet/Mini App interfaces | External receipt + reconciliation |

**Do not run every formula on every packet.** Normalize all allowed evidence, select the relevant existing computation modules by event type and goal, then fuse outputs into a candidate action and verify its authority and effects. A module with unknown inputs emits UNKNOWN, not an invented decision.
