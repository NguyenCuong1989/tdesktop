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
- **Remaining methods:** 849 schema methods are NOT_YET_PROVEN at the AP Omega action boundary, even if Telegram upstream already implements many of them.

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
