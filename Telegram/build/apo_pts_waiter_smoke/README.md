# PTS waiter isolated actual-source smoke

This harness compiles the repository's actual
`Telegram/SourceFiles/data/data_pts_waiter.cpp` with MSVC C++20. A forced
prelude supplies types normally inherited from Telegram's unity build and
includes Telegram's REAL `base::flat_map` implementation from `Telegram/lib_base`, while
shadow include paths provide minimal `Api::Updates`, `Main::Session`, and APΩ
hook spies.

It is BUILD+TEST evidence for the isolated source control flow only. It is not
a native Telegram product build, authenticated runtime test, network test, or
proof of server/model behavior. The fake owner uses account id `424242`; it
does not read credentials or perform outbound work.

The suite covers the baseline cases plus 100 deterministic fixed-seed
shuffles of a contiguous admissible PTS range, reentrant queue cancellation and
recovery, mixed `MTPUpdate` / `MTPUpdates` dispatch origins, the source's
accepted `count == 0` behavior, and three reentrant-arrival regression checks. The shuffle oracle requires every callback
exactly once in monotonic PTS order and observes that at least one arrival was
delayed before reconciliation.

The separately enabled `--probe-reentrant-arrival` case injects synthetic pts14
through `updateAndApply()` while the native handler for queued pts11 is running
and pts12/13 remain queued. It prints only synthetic apply order, current PTS,
and `PTS_HANDLER_RETURNED` marker order. The probe returns nonzero and labels a
source-level `BEHAVIORAL HAZARD` if callbacks are not monotonic and exactly once,
current PTS does not advance to 14, or marker order differs from apply order.
This probe is also part of the default regression suite, alongside grouped-update
and PTS-only reentrant arrivals.

Run the existing script to compile the actual source and execute the
regression suite, then invoke the produced executable explicitly:

```powershell
.\Telegram\build\apo_pts_waiter_smoke.ps1
.\out\apo_pts_waiter_smoke\apo-pts-waiter-smoke.exe --probe-reentrant-arrival
```

The injection is an isolated fake-owner callback model. Whether the same
synchronous arrival occurs in an authenticated native Telegram process remains
`NOT_RUN`; the harness result must not be reported as native or system PASS.

After deferring reentrant arrivals until the queued batch drains, the isolated
probe reports apply order `11,12,13,14`, current PTS `14`, and marker order
`11,12,13,14`; exactly-once and monotonic checks pass. This is source-level
regression evidence only, not a claim that the authenticated native Telegram
runtime or end-to-end MTProto gates have passed.
